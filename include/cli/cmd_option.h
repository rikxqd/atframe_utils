﻿#ifndef _UTIL_CLI_CMDOPTION_H_
#define _UTIL_CLI_CMDOPTION_H_

# pragma once

/**
 * cmd_option_bind<TCmdStr>.h
 * 
 *  Version: 2.0.0
 *  Created on: 2011-12-29
 *  Last edit : 2016-04-09
 *      Author: OWenT
 *
 * 应用程序命令处理
 * 注意: 除了类绑定的目标类外，所有默认的函数推断均是传值方式引入
 *      并且数值的复制在执行bind_cmd时，如果需要引用需要显式指定函数类型
 *      注意默认推导不支持隐式转换(即对double和int是默认推导，但float、short、long等需要指明参数类型)
 *      为了更高效，所有返回值均为PDO类型和指针/智能指针
 *
 */

#include <exception>
#include <set>
#include <cstdio>

#include <vector>
#include <map>

#include "std/ref.h"
#include "std/smart_ptr.h"

// 载入绑定器
#include "cli/cmd_option_bind.h"
#include "cli/cmd_option_string.h"
#include "cli/cmd_option_bindt_base.h"

namespace util {
    namespace cli {
        // 标准指令处理函数(无返回值，参数为选项的映射表)
        // void function_name (cmd_option_list&, [参数]); // 函数参数可选
        // void function_name (callback_param, [参数]); // 函数参数可选

        // 值类型
        typedef std::shared_ptr<cli::cmd_option_value> value_type;


        /**
         * 命令处理函数
         * 内定命令/变量列表(用于处理内部事件):
         *      @OnError    :  出错时触发
         *          @ErrorMsg   : @OnError 函数的错误名称
         *
         *      @OnDefault  :  默认执行函数(用于执行批量命令时的第一个指令前的参数)
         *                     注意：如果第一个参数就是指令则@OnDefault会被传入空参数执行
         *
         *      @OnCallFunc :  分离参数后，转入命令前(将传入所有参数，仅限于子cmd_option_bind<TCmdStr>执行时)
         *                     建议：可以在这个事件响应函数里再绑定其他命令，减少指令冗余
         *                     [注: 调用start函数不会响应这个事件]
         */
        template<typename TCmdStr>
        class cmd_option_bind : public binder::cmd_option_bind_base
        {
        protected:
            typedef unsigned char uc_t;
            static short map_value_[256]; // 记录不同字符的映射关系
            static char trans_value_[256]; // 记录特殊转义字符

            typedef std::map<TCmdStr, std::shared_ptr<binder::cmd_option_bind_base> > funmap_type;
            funmap_type callback_funcs_; // 记录命令的映射函数

            /**
             * 执行命令
             * @param cmd_content 指令名称
             * @param params 指令参数
             */
            void run_cmd(const TCmdStr& cmd_content, callback_param params) const
            {
                typename funmap_type::const_iterator iter = callback_funcs_.find(cmd_content);

                // 如果是顶层调用则添加根指令调用栈
                if (params.get_cmd_array().empty())
                {
                    params.append_cmd(ROOT_NODE_CMD, std::const_pointer_cast<binder::cmd_option_bind_base>(shared_from_this()));
                }

                if (iter == callback_funcs_.end())
                {
                    // 内定命令不报“找不到指令”错
                    if (cmd_content == "@OnDefault")
                        return;

                    iter = callback_funcs_.find("@OnError"); // 查找错误处理函数
                    if (iter != callback_funcs_.end())
                    {
                        // 末尾无参数Key填充空Value
                        if (params.get_params_number() % 2)
                            params.add("");

                        // 错误附加内容(错误内容)
                        params.add("@ErrorMsg");
                        params.add("Command Invalid");
                        params.append_cmd(cmd_content.c_str(), iter->second); // 添加当前指令调用栈

                        (*iter->second)(params);
                    }
                    return;
                }

                // 添加当前指令调用栈
                params.append_cmd(cmd_content.c_str(), iter->second);
                (*iter->second)(params);
            }

            /**
             * 从字符串获取一个字段（返回未处理的字符串结尾）
             * @param begin_str 要解析的字符串的起始位置
             * @param val 解析结果
             * @return 未解析部分的开始位置
             */
            const char* get_segment(const char* begin_str, std::string& val) const
            {
                val.clear();
                char flag;  // 字符串开闭字符

                // 去除分隔符前缀
                while (*begin_str && (map_value_[(uc_t)*begin_str] & SPLITCHAR))
                    ++begin_str;

                while (*begin_str && !(map_value_[(uc_t)*begin_str] & SPLITCHAR))
                {
                    if (!(map_value_[(uc_t)*begin_str] & STRINGSYM))
                    {
                        val += *begin_str;
                        ++begin_str;
                    } else
                    {
                        flag = *begin_str;
                        ++begin_str;

                        while (*begin_str && *begin_str != flag)
                        {
                            char cur_byte = *begin_str;
                            if (map_value_[(uc_t)*begin_str] & TRANSLATE)
                            {
                                if (*(begin_str + 1))
                                {
                                    ++begin_str;
                                    cur_byte = trans_value_[(uc_t)*begin_str];
                                }
                            }

                            val += cur_byte;
                            ++begin_str;
                        }

                        ++begin_str;
                        break;  // 字符串结束后参数结束
                    }
                }

                // 去除分隔符后缀
                while (*begin_str && (map_value_[(uc_t)*begin_str] & SPLITCHAR))
                    ++begin_str;
                return begin_str;
            }

            /**
             * 多指令分离
             * @param begin_str 源字符串
             * @return 分离结果
             */
            std::vector<std::string> split_cmd(const char* begin_str) const
            {
                std::vector<std::string> ret;
                for (const char* begin_ptr = begin_str; (*begin_ptr);)
                {
                    std::string cmd_content;
                    // 去除命令分隔符前缀
                    while ((*begin_ptr) && (map_value_[(uc_t)*begin_ptr] & CMDSPLIT))
                        ++begin_ptr;

                    // 分离命令
                    while ((*begin_ptr) && !(map_value_[(uc_t)*begin_ptr] & CMDSPLIT))
                    {
                        cmd_content.push_back(*begin_ptr);
                        ++begin_ptr;
                    }

                    if (cmd_content.size() > 0)
                        ret.push_back(cmd_content);
                }

                return ret;
            }

            /**
             * 默认帮助函数
             */
            void on_help(callback_param)
            {
                puts("help:");
                puts(get_help_msg().c_str());
            }

        private:
            /**
             * 构造函数
             */
            cmd_option_bind()
            {
                // 如果已初始化则跳过
                if (map_value_[(uc_t)' '] & SPLITCHAR)
                    return;

                // 分隔符
                map_value_[(uc_t)' '] = map_value_[(uc_t)'\t'] =
                    map_value_[(uc_t)'\r'] = map_value_[(uc_t)'\n'] = SPLITCHAR;
                // 字符串开闭符
                map_value_[(uc_t)'\''] = map_value_[(uc_t)'\"'] = STRINGSYM;
                // 转义标记符
                map_value_[(uc_t)'\\'] = TRANSLATE;
                // 指令分隔符
                map_value_[(uc_t)' '] |= CMDSPLIT;
                map_value_[(uc_t)','] = map_value_[(uc_t)';'] = CMDSPLIT;

                // 转义字符设置
                for (int i = 0; i < 256; ++i)
                    trans_value_[i] = i;

                trans_value_[(uc_t)'0'] = '\0';
                trans_value_[(uc_t)'a'] = '\a';
                trans_value_[(uc_t)'b'] = '\b';
                trans_value_[(uc_t)'f'] = '\f';
                trans_value_[(uc_t)'r'] = '\r';
                trans_value_[(uc_t)'n'] = '\n';
                trans_value_[(uc_t)'t'] = '\t';
                trans_value_[(uc_t)'v'] = '\v';
                trans_value_[(uc_t)'\\'] = '\\';
                trans_value_[(uc_t)'\''] = '\'';
                trans_value_[(uc_t)'\"'] = '\"';
            }

        public:
            typedef std::shared_ptr<cmd_option_bind> ptr_type;
            static ptr_type create()
            {
                return ptr_type(new cmd_option_bind());
            }

            /**
             * 获取已绑定的指令列表
             * @return 指令列表指针
             */
            std::shared_ptr<std::vector<const char*> > get_cmd_names() const
            {
                typename funmap_type::const_iterator iter = callback_funcs_.begin();
                std::shared_ptr<std::vector<const char*> > ret_ptr =
                    std::shared_ptr<std::vector<const char*> >(new std::vector<const char*>());
                while (iter != callback_funcs_.end())
                {
                    ret_ptr->push_back(iter->first.c_str());
                    ++iter;
                }
                return ret_ptr;
            }

            /**
             * 获取已绑定的指令对象
             * @param cmd_name 指令名称
             * @return 绑定的指令对象指针, 未找到返回空指针指针
             */
            std::shared_ptr<binder::cmd_option_bind_base> get_binded_cmd(const char* cmd_name) const
            {
                typename funmap_type::const_iterator iter = callback_funcs_.find(cmd_name);
                if (iter == callback_funcs_.end())
                    return std::shared_ptr<binder::cmd_option_bind_base>();
                return iter->second;
            }

            /**
             * 处理指令
             * 说明： 在第一个指令前的参数都将传入@OnDefault事件
             *     参数可通过get[数组下标]获取
             *     第一次使用get[字符串]时将构建参数映射表，get(0)为key，get(1)为value，get(2)为key，get(3)为value，依此类推
             *     如果最后一组key没有value，执行get[key]将返回空指针
             *     注意：get[偶数下标]对应的所有value值和get[字符串]返回的指针共享数据(即改了一个另一个也随之更改)
             */

             /**
              * 处理已分离指令(使用cmd_option_list存储参数集)
              * @param args 数据集合
              * @param is_single_cmd 是否强制单指令, 如果不强制, 则指令名称不能重复
              */
            void start(callback_param args, bool is_single_cmd = false) const
            {
                int argv = static_cast<int>(args.get_params_number());
                cmd_option_list cmd_args;
                TCmdStr cmd_content = is_single_cmd ? "@OnError" : "@OnDefault";
                for (int i = -1; i < argv; )
                {
                    ++i;
                    cmd_args.clear();
                    cmd_args.load_cmd_array(args.get_cmd_array());
                    cmd_args.set_ext_param(args.get_ext_param());

                    for (; i < argv; ++i)
                    {
                        // 把所有的非指令字符串设为指令参数
                        if (callback_funcs_.find(args[i]->to_string()) == callback_funcs_.end())
                        {
                            cmd_args.add(args[i]->to_string());
                        } else
                        {
                            // 如果是单指令且有未知参数则分发@OnError错误处理
                            if (is_single_cmd && cmd_args.get_params_number() > 0)
                            {
                                run_cmd(cmd_content, cmd_args);
                                cmd_args.clear();
                                cmd_args.load_cmd_array(args.get_cmd_array());
                                cmd_args.set_ext_param(args.get_ext_param());
                            }

                            // 追加所有参数，执行单指令
                            if (is_single_cmd)
                            {
                                cmd_content = TCmdStr(args[i]->to_cpp_string().c_str(), args[i]->to_cpp_string().size());
                                for (++i; i < argv; ++i)
                                    cmd_args.add(args[i]->to_string());
                            }
                            break;
                        }
                    }

                    run_cmd(cmd_content, cmd_args);
                    if (i >= argv)
                        break;
                    cmd_content = TCmdStr(args[i]->to_cpp_string().c_str(), args[i]->to_cpp_string().size());
                }
            }

            /**
             * 处理已分离指令(使用char**存储参数集)
             * @param argv 参数个数
             * @param argc 参数列表
             * @param is_single_cmd 是否强制单指令, 如果不强制, 则指令名称不能重复
             * @param ext_param 透传参数
             */
            inline void start(int argv, const char* argc[], bool is_single_cmd = false, void* ext_param = NULL) const
            {
                cmd_option_list copt_list(argv, argc);
                copt_list.set_ext_param(ext_param);

                start(copt_list, is_single_cmd);
            }

            /**
             * 处理已分离指令(使用std::vector<std::string>存储参数集)
             * @param cmds 数据集合
             * @param is_single_cmd 是否强制单指令, 如果不强制, 则指令名称不能重复
             * @param ext_param 透传参数
             */
            inline void start(const std::vector<std::string>& cmds, bool is_single_cmd = false, void* ext_param = NULL) const
            {
                cmd_option_list copt_list(cmds);
                copt_list.set_ext_param(ext_param);

                start(copt_list, is_single_cmd);
            }

            /**
             * 处理未分离指令(使用const char*存储参数集字符串)
             * @param cmd_content 指令
             * @param is_single_cmd 是否强制单指令, 如果不强制, 则指令名称不能重复
             */
            void start(const char* cmd_content, bool is_single_cmd = false, void* ext_param = NULL) const
            {
                cmd_option_list cmds;
                std::string seg;

                // 分离指令
                while (*cmd_content)
                {
                    cmd_content = get_segment(cmd_content, seg);
                    cmds.add(seg.c_str());
                }

                cmds.set_ext_param(ext_param);

                start(cmds, is_single_cmd);
            }

            /**
             * 处理未分离指令(使用const std::string&存储参数集字符串)
             * @param cmd_content 指令
             * @param is_single_cmd 是否强制单指令, 如果不强制, 则指令名称不能重复
             */
            inline void start(const std::string& cmd_content, bool is_single_cmd = false, void* ext_param = NULL) const
            {
                start(cmd_content.c_str(), is_single_cmd, ext_param);
            }

            /**
             * 解绑指令
             * @param cmd_content 指令名称
             */
            inline void unbind_cmd(const std::string& cmd_content)
            {
                TCmdStr cmd_obj = TCmdStr(cmd_content.c_str(), cmd_content.size());
                callback_funcs_.erase(cmd_obj);
            }

            /**
             * 解绑全部指令
             */
            inline void unbind_all_cmd()
            {
                callback_funcs_.clear();
            }

            /**
             * 绑定默认帮助函数
             * @param help_cmd_content 帮助命令名称
             */
            inline std::shared_ptr< binder::cmd_option_bindt<
                typename binder::maybe_wrap_member_pointer<
                void (cmd_option_bind<TCmdStr>::*) (callback_param)>::caller_type,
#if defined(UTIL_CONFIG_COMPILER_CXX_VARIADIC_TEMPLATES) && UTIL_CONFIG_COMPILER_CXX_VARIADIC_TEMPLATES
                binder::cmd_option_bind_param_list< cmd_option_bind<TCmdStr>* >
#else
                binder::cmd_option_bind_param_list1< cmd_option_bind<TCmdStr>* >
#endif
            > >
                bind_help_cmd(const char* help_cmd_content)
            {
                return bind_cmd(help_cmd_content, &cmd_option_bind<TCmdStr>::on_help, this);
            }

            /**
             * 执行子结构
             */
            virtual void operator()(callback_param arg)
            {
                // 响应@OnCallFunc事件
                typename funmap_type::const_iterator iter = callback_funcs_.find("@OnCallFunc");
                if (iter != callback_funcs_.end())
                    (*iter->second)(arg);

                // 重新执行指令集, 进入子结构的一定是单指令
                start(arg, true);
            }

            /**
             * 获取命令集合的帮助信息
             * @param prefix_data 前缀
             */
            virtual std::string get_help_msg(const char* prefix_data = "")
            {
                std::set<typename funmap_type::mapped_type> set_obj;
                std::string help_msg_content;

                for (typename funmap_type::const_iterator iter = callback_funcs_.begin(); iter != callback_funcs_.end(); ++iter)
                {
                    // 删除重复的引用对象
                    if (set_obj.find(iter->second) != set_obj.end())
                        continue;

                    // 跳过内置命令
                    if ('@' == *iter->first.c_str())
                        continue;

                    set_obj.insert(iter->second);
                    std::string cmd_help = iter->second->get_help_msg((prefix_data + help_msg_).c_str());

                    if (cmd_help.size() > 0)
                    {
                        if (help_msg_content.size() > 0 && '\n' != *help_msg_content.rbegin())
                            help_msg_content += "\r\n";
                        help_msg_content += cmd_help;
                    }
                }
                return help_msg_content;
            }

            /**
             * 增加指令处理函数 (相同命令会被覆盖)
             * 支持普通函数和类成员函数
             * 注意：所有传入的类为引用，请确保在执行start时类对象未被释放（特别注意指针和局部变量）
             * 注意：参数的复制发生在执行bind_cmd函数时
             */

             /**
              * 绑定函数对象/函数/成员函数(自适应)
              * 注意：默认会复制函数对象和传入参数
              *
              * bind_cmd: 绑定参数[注意值的复制发生在本函数执行时]
              * example:
              *      *.bind_cmd(命令名称, 函数对象/函数/成员函数, 参数)                           // 默认类型推断是传值而非引用
              *      *.bind_cmd<传入类型>(命令名称, 函数对象/函数/成员函数, 参数)
              *      *.bind_cmd<传入类型, 参数类型>(命令名称, 函数对象/函数/成员函数, 参数)
              */

#if defined(UTIL_CONFIG_COMPILER_CXX_VARIADIC_TEMPLATES) && UTIL_CONFIG_COMPILER_CXX_VARIADIC_TEMPLATES
            template<typename _F, typename... _Args>  // 绑定函数(_Arg:参数[注意值的复制发生在本函数执行时], _R: 绑定函数返回值类型)
            std::shared_ptr<binder::cmd_option_bindt<
                typename binder::maybe_wrap_member_pointer<_F>::caller_type,
                binder::cmd_option_bind_param_list<_Args...>
            > >
                bind_cmd(const std::string& cmd_content, _F raw_fn, _Args... args)
            {
                typedef binder::cmd_option_bind_param_list<_Args...> list_type;
                typedef typename binder::maybe_wrap_member_pointer<_F>::caller_type caller_type;
                typedef std::shared_ptr<binder::cmd_option_bindt<caller_type, list_type> > obj_type;

                obj_type fn = obj_type(new binder::cmd_option_bindt<caller_type, list_type>(caller_type(raw_fn), list_type(args...)));

                std::vector<std::string> cmds = split_cmd(cmd_content.c_str());
                for (std::vector<std::string>::size_type index = 0; index < cmds.size(); ++index)
                {
                    TCmdStr cmd_obj = TCmdStr(cmds[index].c_str(), cmds[index].size());
                    callback_funcs_[cmd_obj] = fn;
                }

                return fn;
            }

#else
            template<typename _F>   // 绑定函数(_F: 函数对象类型)
            std::shared_ptr<binder::cmd_option_bindt<
                typename binder::maybe_wrap_member_pointer<_F>::caller_type,
                binder::cmd_option_bind_param_list0
            > >
                bind_cmd(const std::string& cmd_content, _F raw_fn)
            {
                typedef binder::cmd_option_bind_param_list0 list_type;
                typedef typename binder::maybe_wrap_member_pointer<_F>::caller_type caller_type;
                typedef std::shared_ptr<binder::cmd_option_bindt<caller_type, list_type> > obj_type;

                obj_type fn = obj_type(new binder::cmd_option_bindt<caller_type, list_type>(caller_type(raw_fn), list_type()));

                std::vector<std::string> cmds = split_cmd(cmd_content.c_str());
                for (std::vector<std::string>::size_type index = 0; index < cmds.size(); ++index)
                {
                    TCmdStr cmd_obj = TCmdStr(cmds[index].c_str(), cmds[index].size());
                    callback_funcs_[cmd_obj] = fn;
                }

                return fn;
            }

            template<typename _F, typename _Arg0>  // 绑定函数(_Arg:参数[注意值的复制发生在本函数执行时], _R: 绑定函数返回值类型)
            std::shared_ptr<binder::cmd_option_bindt<
                typename binder::maybe_wrap_member_pointer<_F>::caller_type,
                binder::cmd_option_bind_param_list1<_Arg0>
            > >
                bind_cmd(const std::string& cmd_content, _F raw_fn, _Arg0 arg0)
            {
                typedef binder::cmd_option_bind_param_list1<_Arg0> list_type;
                typedef typename binder::maybe_wrap_member_pointer<_F>::caller_type caller_type;
                typedef std::shared_ptr<binder::cmd_option_bindt<caller_type, list_type> > obj_type;

                obj_type fn = obj_type(new binder::cmd_option_bindt<caller_type, list_type>(caller_type(raw_fn), list_type(arg0)));

                std::vector<std::string> cmds = split_cmd(cmd_content.c_str());
                for (std::vector<std::string>::size_type index = 0; index < cmds.size(); ++index)
                {
                    TCmdStr cmd_obj = TCmdStr(cmds[index].c_str(), cmds[index].size());
                    callback_funcs_[cmd_obj] = fn;
                }

                return fn;
            }

            template<typename _F, typename _Arg0, typename _Arg1>  // 绑定函数(_Arg:参数[注意值的复制发生在本函数执行时], _R: 绑定函数返回值类型)
            std::shared_ptr<binder::cmd_option_bindt<
                typename binder::maybe_wrap_member_pointer<_F>::caller_type,
                binder::cmd_option_bind_param_list2<_Arg0, _Arg1>
            > >
                bind_cmd(const std::string& cmd_content, _F raw_fn, _Arg0 arg0, _Arg1 arg1)
            {
                typedef binder::cmd_option_bind_param_list2<_Arg0, _Arg1> list_type;
                typedef typename binder::maybe_wrap_member_pointer<_F>::caller_type caller_type;
                typedef std::shared_ptr<binder::cmd_option_bindt<caller_type, list_type> > obj_type;

                obj_type fn = obj_type(new binder::cmd_option_bindt<caller_type, list_type>(caller_type(raw_fn), list_type(arg0, arg1)));

                std::vector<std::string> cmds = split_cmd(cmd_content.c_str());
                for (std::vector<std::string>::size_type index = 0; index < cmds.size(); ++index)
                {
                    TCmdStr cmd_obj = TCmdStr(cmds[index].c_str(), cmds[index].size());
                    callback_funcs_[cmd_obj] = fn;
                }

                return fn;
            }

            template<typename _F, typename _Arg0, typename _Arg1, typename _Arg2>  // 绑定函数(_Arg:参数[注意值的复制发生在本函数执行时], _R: 绑定函数返回值类型)
            std::shared_ptr<binder::cmd_option_bindt<
                typename binder::maybe_wrap_member_pointer<_F>::caller_type,
                binder::cmd_option_bind_param_list3<_Arg0, _Arg1, _Arg2>
            > >
                bind_cmd(const std::string& cmd_content, _F raw_fn, _Arg0 arg0, _Arg1 arg1, _Arg2 arg2)
            {
                typedef binder::cmd_option_bind_param_list3<_Arg0, _Arg1, _Arg2> list_type;
                typedef typename binder::maybe_wrap_member_pointer<_F>::caller_type caller_type;
                typedef std::shared_ptr<binder::cmd_option_bindt<caller_type, list_type> > obj_type;

                obj_type fn = obj_type(new binder::cmd_option_bindt<caller_type, list_type>(caller_type(raw_fn), list_type(arg0, arg1, arg2)));

                std::vector<std::string> cmds = split_cmd(cmd_content.c_str());
                for (std::vector<std::string>::size_type index = 0; index < cmds.size(); ++index)
                {
                    TCmdStr cmd_obj = TCmdStr(cmds[index].c_str(), cmds[index].size());
                    callback_funcs_[cmd_obj] = fn;
                }

                return fn;
            }

            template<typename _F, typename _Arg0, typename _Arg1, typename _Arg2, typename _Arg3>  // 绑定函数(_Arg:参数[注意值的复制发生在本函数执行时], _R: 绑定函数返回值类型)
            std::shared_ptr<binder::cmd_option_bindt<
                typename binder::maybe_wrap_member_pointer<_F>::caller_type,
                binder::cmd_option_bind_param_list4<_Arg0, _Arg1, _Arg2, _Arg3>
            > >
                bind_cmd(const std::string& cmd_content, _F raw_fn, _Arg0 arg0, _Arg1 arg1, _Arg2 arg2, _Arg3 arg3)
            {
                typedef binder::cmd_option_bind_param_list4<_Arg0, _Arg1, _Arg2, _Arg3> list_type;
                typedef typename binder::maybe_wrap_member_pointer<_F>::caller_type caller_type;
                typedef std::shared_ptr<binder::cmd_option_bindt<caller_type, list_type> > obj_type;

                obj_type fn = obj_type(new binder::cmd_option_bindt<caller_type, list_type>(caller_type(raw_fn), list_type(arg0, arg1, arg2, arg3)));

                std::vector<std::string> cmds = split_cmd(cmd_content.c_str());
                for (std::vector<std::string>::size_type index = 0; index < cmds.size(); ++index)
                {
                    TCmdStr cmd_obj = TCmdStr(cmds[index].c_str(), cmds[index].size());
                    callback_funcs_[cmd_obj] = fn;
                }

                return fn;
            }
#endif

            /**
             * 绑定指令(通用)
             * bind_cmd: 绑定参数
             * example:
             *      *.bind_cmd(命令名称, binder::cmd_option_bind_base 结构智能指针)
             *      *.bind_cmd(命令名称, cmd_option_bind<TCmdStr> 结构引用)
             * 推荐使用上一种，可以减少一次结构复制
             */
            std::shared_ptr<binder::cmd_option_bind_base> bind_child_cmd(const std::string cmd_content, std::shared_ptr<binder::cmd_option_bind_base> base_node)
            {
                std::vector<std::string> cmds = split_cmd(cmd_content.c_str());
                for (std::vector<std::string>::size_type index = 0; index < cmds.size(); ++index)
                {
                    TCmdStr cmd_obj = TCmdStr(cmds[index].c_str(), cmds[index].size());
                    callback_funcs_[cmd_obj] = base_node;
                }

                return base_node;
            }
            std::shared_ptr<binder::cmd_option_bind_base> bind_child_cmd(const std::string cmd_content, const cmd_option_bind<TCmdStr>& cmd_opt)
            {
                std::shared_ptr<binder::cmd_option_bind_base> base_node = std::shared_ptr<cmd_option_bind<TCmdStr> >(new cmd_option_bind<TCmdStr>(cmd_opt));
                std::vector<std::string> cmds = split_cmd(cmd_content.c_str());
                for (std::vector<std::string>::size_type index = 0; index < cmds.size(); ++index)
                {
                    TCmdStr cmd_obj = TCmdStr(cmds[index].c_str(), cmds[index].size());
                    callback_funcs_[cmd_obj] = base_node;
                }

                return base_node;
            }
        };

        template<typename Ty>
        short cmd_option_bind<Ty>::map_value_[256] = { 0 };

        template<typename Ty>
        char cmd_option_bind<Ty>::trans_value_[256] = { 0 };

        // 类型重定义
        typedef cmd_option_bind<std::string> cmd_option;
        typedef cmd_option_bind<cmd_option_ci_string> cmd_option_ci;
    }

}
#endif /* CMDOPTION_H_ */
