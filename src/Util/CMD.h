/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SRC_UTIL_CMD_H_
#define SRC_UTIL_CMD_H_

#include <map>
#include <mutex>
#include <string>
#include <memory>
#include <vector>
#include <iosfwd>
#include <functional>
#include "mini.h"

namespace toolkit {

class Option {
public:
    using OptionHandler = std::function<bool(const std::shared_ptr<std::ostream> &stream, const std::string &arg)>;

    enum ArgType {
        ArgNone = 0,//no_argument,
        ArgRequired = 1,//required_argument,
        ArgOptional = 2,//optional_argument
    };

    Option() = default;
    Option(char short_opt, const char *long_opt, enum ArgType type, const char *default_value, bool must_exist,
           const char *des, const OptionHandler &cb);

    bool operator()(const std::shared_ptr<std::ostream> &stream, const std::string &arg);

private:
    friend class OptionParser;
    bool _must_exist = false;
    char _short_opt;
    enum ArgType _type;
    std::string _des;
    std::string _long_opt;
    OptionHandler _cb;
    std::shared_ptr<std::string> _default_value;
};

class OptionParser {
public:
    using OptionCompleted = std::function<void(const std::shared_ptr<std::ostream> &, mINI &)>;

    OptionParser(const OptionCompleted &cb = nullptr, bool enable_empty_args = true);
    OptionParser &operator<<(Option &&option);
    OptionParser &operator<<(const Option &option);

    void delOption(const char *key);

    void operator ()(mINI &all_args, int argc, char *argv[], const std::shared_ptr<std::ostream> &stream);
private:
    bool _enable_empty_args;
    Option _helper;
    std::map<char, int> _map_char_index;
    std::map<int, Option> _map_options;
    OptionCompleted _on_completed;
};

class CMD : public mINI {
public:
    virtual ~CMD() = default;

    virtual const char *description() const;

    void operator()(int argc, char *argv[], const std::shared_ptr<std::ostream> &stream = nullptr);

    bool hasKey(const char *key);

    std::vector<variant> splitedVal(const char *key, const char *delim = ":");

    void delOption(const char *key);

protected:
    std::shared_ptr<OptionParser> _parser;

private:
    void split(const std::string &s, const char *delim, std::vector<variant> &ret);
};

class CMDRegister {
public:
    static CMDRegister &Instance();

    void clear();

    void registCMD(const char *name, const std::shared_ptr<CMD> &cmd);
    void unregistCMD(const char *name);

    std::shared_ptr<CMD> operator[](const char *name);

    void operator()(const char *name, int argc, char *argv[], const std::shared_ptr<std::ostream> &stream = nullptr);
    void operator()(const std::string &line, const std::shared_ptr<std::ostream> &stream = nullptr);

    void printHelp(const std::shared_ptr<std::ostream> &streamTmp = nullptr);
private:
    size_t getArgs(char *buf, std::vector<char *> &argv);

private:
    std::recursive_mutex _mtx;
    std::map<std::string, std::shared_ptr<CMD> > _cmd_map;
};

//帮助命令(help)，该命令默认已注册
class CMD_help : public CMD {
public:
    CMD_help() {
        _parser = std::make_shared<OptionParser>([](const std::shared_ptr<std::ostream> &stream, mINI &) {
            CMDRegister::Instance().printHelp(stream);
        });
    }

    const char *description() const override {
        return "打印帮助信息";
    }
};

class ExitException : public std::exception {};

//退出程序命令(exit)，该命令默认已注册
class CMD_exit : public CMD {
public:
    CMD_exit() {
        _parser = std::make_shared<OptionParser>([](const std::shared_ptr<std::ostream> &, mINI &) {
            throw ExitException();
        });
    }

    const char *description() const override {
        return "退出shell";
    }
};

//退出程序命令(quit),该命令默认已注册
#define CMD_quit CMD_exit

//清空屏幕信息命令(clear)，该命令默认已注册
class CMD_clear : public CMD {
public:
    CMD_clear() {
        _parser = std::make_shared<OptionParser>([this](const std::shared_ptr<std::ostream> &stream, mINI &args) {
            clear(stream);
        });
    }

    const char *description() const {
        return "清空屏幕输出";
    }

private:
    void clear(const std::shared_ptr<std::ostream> &stream) {
        (*stream) << "\x1b[2J\x1b[H";
        stream->flush();
    }
};

#define GET_CMD(name) (*(CMDRegister::Instance()[name]))
#define CMD_DO(name,...) (*(CMDRegister::Instance()[name]))(__VA_ARGS__)
#define REGIST_CMD(name) CMDRegister::Instance().registCMD(#name,std::make_shared<CMD_##name>());

}//namespace toolkit
#endif /* SRC_UTIL_CMD_H_ */
