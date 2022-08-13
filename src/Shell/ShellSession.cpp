﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "ShellSession.h"
#include "Util/CMD.h"
#include "Util/onceToken.h"
#include "Util/NoticeCenter.h"
#include "Common/config.h"
#include "ShellCMD.h"

using namespace std;
using namespace toolkit;

namespace mediakit {
class ExitException : public std::exception {};
static onceToken s_token([]() {
    REGIST_CMD(media);
}, nullptr);

ShellSession::ShellSession(hio_t* io) : Session(io) {
    DebugP(this);
    pleaseInputUser();
}

ShellSession::~ShellSession() {
    DebugP(this);
}

void ShellSession::onRecv(const char* buf, int size) {
    //DebugL << hexdump(buf->data(), buf->size());
    GET_CONFIG(uint32_t,maxReqSize,Shell::kMaxReqSize);
    if (_strRecvBuf.size() + size >= maxReqSize) {
        shutdown(SockException(Err_other,"recv buffer overflow"));
        return;
    }
    _beatTicker.resetTime();
    _strRecvBuf.append(buf, size);
    if (_strRecvBuf.find("\xff\xf4\xff\0xfd\x06") != std::string::npos) {
        write("\033[0m\r\n	Bye bye!\r\n");
        shutdown(SockException(Err_other,"received Ctrl+C"));
        return;
    }
    size_t index;
    string line;
    while ((index = _strRecvBuf.find("\r\n")) != std::string::npos) {
        line = _strRecvBuf.substr(0, index);
        _strRecvBuf.erase(0, index + 2);
        if (!onCommandLine(line)) {
            shutdown(SockException(Err_other,"exit cmd"));
            return;
        }
    }
}

void ShellSession::onError(const SockException &err){
    WarnP(this) << err.what();
}

void ShellSession::onManager() {
    if (_beatTicker.elapsedTime() > 1000 * 60 * 5) {
        //5 miniutes for alive
        shutdown(SockException(Err_timeout,"session timeout"));
        return;
    }
}

inline bool ShellSession::onCommandLine(const string& line) {
    auto loginInterceptor = _loginInterceptor;
    if (loginInterceptor) {
        bool ret = loginInterceptor(line);
        return ret;
    }
    try {
        std::shared_ptr<stringstream> ss(new stringstream);
        //CMDRegister::Instance()(line,ss);
        write(ss->str());
    }catch(ExitException &){
        return false;
    }catch(std::exception &ex){
        write(ex.what());
        write("\r\n");
    }
    printShellPrefix();
    return true;
}

inline void ShellSession::pleaseInputUser() {
    write("\033[0m");
    write(StrPrinter << kServerName << " login: " << endl);
    _loginInterceptor = [this](const string &user_name) {
        _strUserName=user_name;
        pleaseInputPasswd();
        return true;
    };
}
inline void ShellSession::pleaseInputPasswd() {
    write("Password: \033[8m");
    _loginInterceptor = [this](const string &passwd) {
        auto onAuth = [this](const string &errMessage){
            if(!errMessage.empty()){
                //鉴权失败
                write(StrPrinter
                                 << "\033[0mAuth failed("
                                 << errMessage
                                 << "), please try again.\r\n"
                                 << _strUserName << "@" << kServerName
                                 << "'s password: \033[8m"
                                 << endl);
                return;
            }
            write("\033[0m");
            write("-----------------------------------------\r\n");
            write(StrPrinter<<"欢迎来到"<<kServerName<<", 你可输入\"help\"查看帮助.\r\n"<<endl);
            write("-----------------------------------------\r\n");
            printShellPrefix();
            _loginInterceptor=nullptr;
        };

        std::weak_ptr<ShellSession> weakSelf = std::dynamic_pointer_cast<ShellSession>(shared_from_this());
        Broadcast::AuthInvoker invoker = [weakSelf,onAuth](const string &errMessage){
            auto strongSelf =  weakSelf.lock();
            if(!strongSelf){
                return;
            }
            strongSelf->async([errMessage,weakSelf,onAuth](){
                auto strongSelf =  weakSelf.lock();
                if(!strongSelf){
                    return;
                }
                onAuth(errMessage);
            });
        };

        auto flag = NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastShellLogin,_strUserName,passwd,invoker,static_cast<SockInfo &>(*this));
        if(!flag){
            //如果无人监听shell登录事件，那么默认shell无法登录
            onAuth("please listen kBroadcastShellLogin event");
        }
        return true;
    };
}

void ShellSession::printShellPrefix() {
    write(StrPrinter << _strUserName << "@" << kServerName << "# " << endl);
}

}/* namespace mediakit */
