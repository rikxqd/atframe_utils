﻿#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <sstream>

#include "lock/lock_holder.h"
#include "common/file_system.h"
#include "time/time_utility.h"

#include "log/log_sink_file_backend.h"

// 默认文件大小是256KB
#define DEFAULT_FILE_SIZE 256 * 1024

namespace util {
    namespace log {

        log_sink_file_backend::log_sink_file_backend()
            : rotation_size_(10),    // 默认10个文件
            max_file_size_(DEFAULT_FILE_SIZE), // 默认文件大小
            check_interval_(60), // 默认文件切换检查周期为60秒
            check_expire_point_(0), inited_(false) {

            path_pattern_ = "%Y-%m-%d.%N.log";// 默认文件名规则

            log_file_.auto_flush = false;
            log_file_.rotation_index = 0;
            log_file_.written_size = 0;
        }

        log_sink_file_backend::log_sink_file_backend(const std::string &file_name_pattern)
            : rotation_size_(10),    // 默认10个文件
            max_file_size_(DEFAULT_FILE_SIZE), // 默认文件大小
            check_interval_(60), // 默认文件切换检查周期为60秒
            check_expire_point_(0), inited_(false) {

            log_file_.auto_flush = false;
            log_file_.rotation_index = 0;
            log_file_.written_size = 0;

            set_file_pattern(file_name_pattern);
        }

        log_sink_file_backend::log_sink_file_backend(const log_sink_file_backend& other)
            : rotation_size_(other.rotation_size_),     // 默认文件数量
            max_file_size_(other.max_file_size_),       // 默认文件大小
            check_interval_(other.check_interval_),     // 默认文件切换检查周期为60秒
            check_expire_point_(0), inited_(false) {
            path_pattern_ = other.path_pattern_;

            log_file_.auto_flush = other.log_file_.auto_flush;

            // 其他的部分都要重新初始化，不能复制
        }

        log_sink_file_backend::~log_sink_file_backend() {}

        void log_sink_file_backend::set_file_pattern(const std::string &file_name_pattern) {
            path_pattern_ = file_name_pattern;

            // 设置文件路径模式， 如果文件已打开，需要重新执行初始化流程
            if (log_file_.opened_file) {
                inited_ = false;
                init();
            }
        }

        void log_sink_file_backend::operator()(const log_formatter::caller_info_t &caller, const char *content, size_t content_size) {
            if (!inited_) {
                init();
            }
            
            if (log_file_.written_size > 0 && log_file_.written_size >= max_file_size_) {
                rotate_log();
            }
            check_update();

            std::shared_ptr<std::ofstream> f = open_log_file(true);

            if (!f) {
                return;
            }

            f->write(content, content_size);
            f->put('\n');
            if (log_file_.auto_flush) {
                f->flush();
            }

            log_file_.written_size += content_size + 1;
        }

        void log_sink_file_backend::init() {
            if (inited_) {
                return;
            }

            inited_ = true;

            check_expire_point_ = 0;
            log_file_.rotation_index = 0;
            reset_log_file();

            log_formatter::caller_info_t caller;
            char log_file[file_system::MAX_PATH_LEN];

            for (size_t i = 0; max_file_size_ > 0 && i < rotation_size_; ++i) {
                caller.rotate_index = (log_file_.rotation_index + i) % rotation_size_;
                size_t fsz = 0;
                log_formatter::format(log_file, sizeof(log_file), path_pattern_.c_str(), path_pattern_.size(), caller);
                file_system::file_size(log_file, fsz);

                // 文件不存在fsz也是0
                if (fsz < max_file_size_) {
                    log_file_.rotation_index = caller.rotate_index;
                    break;
                }
            }

            open_log_file(false);
        }

        std::shared_ptr<std::ofstream> log_sink_file_backend::open_log_file(bool destroy_content) {
            if (log_file_.opened_file && log_file_.opened_file->good()) {
                return log_file_.opened_file;
            }

            reset_log_file();

            // 打开新文件要加锁
            lock::lock_holder<lock::spin_lock> lkholder(fs_lock_);

            char log_file[file_system::MAX_PATH_LEN];
            log_formatter::caller_info_t caller;
            caller.rotate_index = log_file_.rotation_index;
            size_t file_path_len = log_formatter::format(log_file, sizeof(log_file), path_pattern_.c_str(), path_pattern_.size(), caller);
            if (file_path_len <= 0) {
                std::cerr << "log.format " << path_pattern_ << " failed"<< std::endl;
                return std::shared_ptr<std::ofstream>();
            }

            std::shared_ptr<std::ofstream> of = std::make_shared<std::ofstream>();
            if (!of) {
                std::cerr << "log.file malloc failed" << path_pattern_ << std::endl;
                return std::shared_ptr<std::ofstream>();
            }

            std::string dir_name;
            util::file_system::dirname(log_file, file_path_len, dir_name);
            if (!dir_name.empty() && !util::file_system::is_exist(dir_name.c_str())) {
                util::file_system::mkdir(dir_name.c_str(), true);
            }

            // 销毁原先的内容
            if (destroy_content) {
                of->open(log_file, std::ios::binary | std::ios::out | std::ios::trunc);
                if (!of->is_open()) {
                    std::cerr << "log.file open " << static_cast<const char*>(log_file) << " failed" << path_pattern_ << std::endl;
                    return std::shared_ptr<std::ofstream>();
                }
                of->close();
            }

            of->open(log_file, std::ios::binary | std::ios::out | std::ios::app);
            if (!of->is_open()) {
                std::cerr << "log.file open "<< static_cast<const char*>(log_file) <<" failed" << path_pattern_ << std::endl;
                return std::shared_ptr<std::ofstream>();
            }

            of->seekp(0, std::ios_base::end);
            log_file_.written_size = static_cast<size_t>(of->tellp());

            log_file_.opened_file = of;
            log_file_.file_path.assign(log_file, file_path_len);
            return log_file_.opened_file;
        }

        void log_sink_file_backend::rotate_log() {
            if (rotation_size_ > 0) {
                log_file_.rotation_index = (log_file_.rotation_index + 1) % rotation_size_;
            } else {
                log_file_.rotation_index = 0;
            }
            reset_log_file();
            check_expire_point_ = 0;
        }

        void log_sink_file_backend::check_update() {
            if (0 != check_expire_point_) {
                if (0 == check_interval_ || util::time::time_utility::get_now() < check_expire_point_) {
                    return;
                }
            }

            check_expire_point_ = util::time::time_utility::get_now() + check_interval_;
            char log_file[file_system::MAX_PATH_LEN];
            log_formatter::caller_info_t caller;
            caller.rotate_index = log_file_.rotation_index;
            size_t file_path_len = log_formatter::format(log_file, sizeof(log_file), path_pattern_.c_str(), path_pattern_.size(), caller);
            if (file_path_len <= 0) {
                return;
            }

            std::string new_file_path;
            std::string old_file_path;

            {
                // 短时间加锁，防止文件路径变更
                lock::lock_holder<lock::spin_lock> lkholder(fs_lock_);
                old_file_path = log_file_.file_path;
            }

            new_file_path.assign(log_file, file_path_len);
            if (new_file_path == old_file_path) {
                return;
            }

            std::string new_dir;
            std::string old_dir;
            util::file_system::dirname(new_file_path.c_str(), new_file_path.size(), new_dir);
            util::file_system::dirname(old_file_path.c_str(), old_file_path.size(), old_dir);

            // 如果目录变化则重置序号
            if (new_dir != old_dir) {
                log_file_.rotation_index = 0;
            }

            reset_log_file();
        }

        void log_sink_file_backend::reset_log_file() {
            // 更换日志文件需要加锁
            lock::lock_holder<lock::spin_lock> lkholder(fs_lock_);

            // 必须依赖析构来关闭文件，以防这个文件正在其他地方被引用
            log_file_.opened_file.reset();
            log_file_.written_size = 0;
            //log_file_.file_path.clear(); // 保留上一个文件路径，即便已被关闭。用于rotate后的目录变更判定
        }
    }
}
