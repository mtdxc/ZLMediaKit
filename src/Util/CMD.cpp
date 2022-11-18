/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "CMD.h"
#include "onceToken.h"
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#include "misc/win32_getopt.h"
#else
#include <getopt.h>
#endif // defined(_WIN32)

using namespace std;

namespace toolkit {

//默认注册exit/quit/help/clear命令
static onceToken s_token([]() {
    REGIST_CMD(exit)
    REGIST_CMD(quit)
    REGIST_CMD(help)
    REGIST_CMD(clear)
});

CMDRegister &CMDRegister::Instance() {
    static CMDRegister instance;
    return instance;
}

void CMDRegister::clear()
{
    std::lock_guard<std::recursive_mutex> lck(_mtx);
    _cmd_map.clear();
}

void CMDRegister::registCMD(const char *name, const std::shared_ptr<CMD> &cmd)
{
    std::lock_guard<std::recursive_mutex> lck(_mtx);
    _cmd_map.emplace(name, cmd);
}

void CMDRegister::unregistCMD(const char *name)
{
    std::lock_guard<std::recursive_mutex> lck(_mtx);
    _cmd_map.erase(name);
}

std::shared_ptr<CMD> CMDRegister::operator[](const char *name)
{
    std::lock_guard<std::recursive_mutex> lck(_mtx);
    auto it = _cmd_map.find(name);
    if (it == _cmd_map.end()) {
        throw std::invalid_argument(std::string("命令不存在:") + name);
    }
    return it->second;
}

void CMDRegister::operator()(const std::string &line, const std::shared_ptr<std::ostream> &stream /*= nullptr*/)
{
    if (line.empty()) {
        return;
    }
    std::vector<char *> argv;
    size_t argc = getArgs((char *)line.data(), argv);
    if (argc == 0) {
        return;
    }
    std::string cmd = argv[0];
    std::lock_guard<std::recursive_mutex> lck(_mtx);
    auto it = _cmd_map.find(cmd);
    if (it == _cmd_map.end()) {
        std::stringstream ss;
        ss << "  未识别的命令\"" << cmd << "\",输入 \"help\" 获取帮助.";
        throw std::invalid_argument(ss.str());
    }
    (*it->second)((int)argc, &argv[0], stream);
}

void CMDRegister::operator()(const char *name, int argc, char *argv[], const std::shared_ptr<std::ostream> &stream /*= nullptr*/)
{
    auto cmd = (*this)[name];
    if (!cmd) {
        throw std::invalid_argument(std::string("命令不存在:") + name);
    }
    (*cmd)(argc, argv, stream);
}

void CMDRegister::printHelp(const std::shared_ptr<std::ostream> &streamTmp /*= nullptr*/)
{
    auto stream = streamTmp;
    if (!stream) {
        stream.reset(&std::cout, [](std::ostream *) {});
    }

    std::lock_guard<std::recursive_mutex> lck(_mtx);
    size_t maxLen = 0;
    for (auto &pr : _cmd_map) {
        if (pr.first.size() > maxLen) {
            maxLen = pr.first.size();
        }
    }
    for (auto &pr : _cmd_map) {
        (*stream) << "  " << pr.first;
        for (size_t i = 0; i < maxLen - pr.first.size(); ++i) {
            (*stream) << " ";
        }
        (*stream) << "  " << pr.second->description() << std::endl;
    }
}

size_t CMDRegister::getArgs(char *buf, std::vector<char *> &argv)
{
    size_t argc = 0;
    bool start = false;
    auto len = strlen(buf);
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\r' && buf[i] != '\n') {
            if (!start) {
                start = true;
                if (argv.size() < argc + 1) {
                    argv.resize(argc + 1);
                }
                argv[argc++] = buf + i;
            }
        }
        else {
            buf[i] = '\0';
            start = false;
        }
    }
    return argc;
}

OptionParser::OptionParser(const OptionCompleted &cb /*= nullptr*/, bool enable_empty_args /*= true*/)
{
    _on_completed = cb;
    _enable_empty_args = enable_empty_args;
    _helper = Option('h', "help", Option::ArgNone, nullptr, false, "打印此信息",
        [this](const std::shared_ptr<std::ostream> &stream, const std::string &arg)->bool {
            static const char *argsType[] = { "无参", "有参", "选参" };
            static const char *mustExist[] = { "选填", "必填" };
            static std::string defaultPrefix = "默认:";
            static std::string defaultNull = "null";

            std::stringstream printer;
            size_t maxLen_longOpt = 0;
            auto maxLen_default = defaultNull.size();

            for (auto &pr : _map_options) {
                auto &opt = pr.second;
                if (opt._long_opt.size() > maxLen_longOpt) {
                    maxLen_longOpt = opt._long_opt.size();
                }
                if (opt._default_value) {
                    if (opt._default_value->size() > maxLen_default) {
                        maxLen_default = opt._default_value->size();
                    }
                }
            }
            for (auto &pr : _map_options) {
                auto &opt = pr.second;
                //打印短参和长参名
                if (opt._short_opt) {
                    printer << "  -" << opt._short_opt << "  --" << opt._long_opt;
                }
                else {
                    printer << "   " << " " << "  --" << opt._long_opt;
                }
                for (size_t i = 0; i < maxLen_longOpt - opt._long_opt.size(); ++i) {
                    printer << " ";
                }
                //打印是否有参
                printer << "  " << argsType[opt._type];
                //打印默认参数
                std::string defaultValue = defaultNull;
                if (opt._default_value) {
                    defaultValue = *opt._default_value;
                }
                printer << "  " << defaultPrefix << defaultValue;
                for (size_t i = 0; i < maxLen_default - defaultValue.size(); ++i) {
                    printer << " ";
                }
                //打印是否必填参数
                printer << "  " << mustExist[opt._must_exist];
                //打印描述
                printer << "  " << opt._des << std::endl;
            }
            throw std::invalid_argument(printer.str());
        });
    (*this) << _helper;
}

OptionParser & OptionParser::operator<<(const Option &option)
{
    int index = 0xFF + (int)_map_options.size();
    if (option._short_opt) {
        _map_char_index.emplace(option._short_opt, index);
    }
    _map_options.emplace(index, option);
    return *this;
}

OptionParser & OptionParser::operator<<(Option &&option)
{
    int index = 0xFF + (int)_map_options.size();
    if (option._short_opt) {
        _map_char_index.emplace(option._short_opt, index);
    }
    _map_options.emplace(index, std::forward<Option>(option));
    return *this;
}

void OptionParser::delOption(const char *key)
{
    for (auto &pr : _map_options) {
        if (pr.second._long_opt == key) {
            if (pr.second._short_opt) {
                _map_char_index.erase(pr.second._short_opt);
            }
            _map_options.erase(pr.first);
            break;
        }
    }
}

void OptionParser::operator()(mINI &all_args, int argc, char *argv[], const std::shared_ptr<ostream> &stream) {
    vector<struct option> vec_long_opt;
    string str_short_opt;
    do {
        struct option tmp;
        for (auto &pr : _map_options) {
            auto &opt = pr.second;
            //long opt
            tmp.name = (char *) opt._long_opt.data();
            tmp.has_arg = opt._type;
            tmp.flag = nullptr;
            tmp.val = pr.first;
            vec_long_opt.emplace_back(tmp);
            //short opt
            if (!opt._short_opt) {
                continue;
            }
            str_short_opt.push_back(opt._short_opt);
            switch (opt._type) {
                case Option::ArgRequired: str_short_opt.append(":"); break;
                case Option::ArgOptional: str_short_opt.append("::"); break;
                default: break;
            }
        }
        tmp.flag = 0;
        tmp.name = 0;
        tmp.has_arg = 0;
        tmp.val = 0;
        vec_long_opt.emplace_back(tmp);
    } while (0);

    static mutex s_mtx_opt;
    lock_guard<mutex> lck(s_mtx_opt);

    int index;
    optind = 0;
    opterr = 0;
    while ((index = getopt_long(argc, argv, &str_short_opt[0], &vec_long_opt[0], nullptr)) != -1) {
        stringstream ss;
        ss << "  未识别的选项,输入\"-h\"获取帮助.";
        if (index < 0xFF) {
            //短参数
            auto it = _map_char_index.find(index);
            if (it == _map_char_index.end()) {
                throw std::invalid_argument(ss.str());
            }
            index = it->second;
        }

        auto it = _map_options.find(index);
        if (it == _map_options.end()) {
            throw std::invalid_argument(ss.str());
        }
        auto &opt = it->second;
        auto pr = all_args.emplace(opt._long_opt, optarg ? optarg : "");
        if (!opt(stream, pr.first->second)) {
            return;
        }
        optarg = nullptr;
    }
    for (auto &pr : _map_options) {
        if (pr.second._default_value && all_args.find(pr.second._long_opt) == all_args.end()) {
            //有默认值,赋值默认值
            all_args.emplace(pr.second._long_opt, *pr.second._default_value);
        }
    }
    for (auto &pr : _map_options) {
        if (pr.second._must_exist) {
            if (all_args.find(pr.second._long_opt) == all_args.end()) {
                stringstream ss;
                ss << "  参数\"" << pr.second._long_opt << "\"必须提供,输入\"-h\"选项获取帮助";
                throw std::invalid_argument(ss.str());
            }
        }
    }
    if (all_args.empty() && _map_options.size() > 1 && !_enable_empty_args) {
        _helper(stream, "");
        return;
    }
    if (_on_completed) {
        _on_completed(stream, all_args);
    }
}

const char * CMD::description() const
{
    return "description";
}

void CMD::operator()(int argc, char *argv[], const std::shared_ptr<std::ostream> &stream /*= nullptr*/)
{
    this->clear();
    std::shared_ptr<std::ostream> coutPtr(&std::cout, [](std::ostream *) {});
    (*_parser)(*this, argc, argv, stream ? stream : coutPtr);
}

bool CMD::hasKey(const char *key)
{
    return this->find(key) != this->end();
}

std::vector<variant> CMD::splitedVal(const char *key, const char *delim /*= ":"*/)
{
    std::vector<variant> ret;
    auto &val = (*this)[key];
    split(val, delim, ret);
    return ret;
}

void CMD::delOption(const char *key)
{
    if (_parser) {
        _parser->delOption(key);
    }
}

void CMD::split(const std::string &s, const char *delim, std::vector<variant> &ret)
{
    size_t last = 0;
    auto index = s.find(delim, last);
    while (index != std::string::npos) {
        if (index - last > 0) {
            ret.push_back(s.substr(last, index - last));
        }
        last = index + strlen(delim);
        index = s.find(delim, last);
    }
    if (s.size() - last > 0) {
        ret.push_back(s.substr(last));
    }
}


Option::Option(char short_opt, const char *long_opt, enum ArgType type, const char *default_value, bool must_exist, const char *des, const OptionHandler &cb)
{
    _short_opt = short_opt;
    _long_opt = long_opt;
    _type = type;
    if (type != ArgNone) {
        if (default_value) {
            _default_value = std::make_shared<std::string>(default_value);
        }
        if (!_default_value && must_exist) {
            _must_exist = true;
        }
    }
    _des = des;
    _cb = cb;
}

bool Option::operator()(const std::shared_ptr<std::ostream> &stream, const std::string &arg)
{
    return _cb ? _cb(stream, arg) : true;
}

}//namespace toolkit