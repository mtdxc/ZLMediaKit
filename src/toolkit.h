#ifndef __SRC_TOOLKIT_H__
#define __SRC_TOOLKIT_H__

#include <string>
#include <exception>
#include <memory>
#include <functional>
typedef struct hio_s hio_t;
namespace hv {
class SocketChannel;
class EventLoop;
}

namespace toolkit {
//错误类型枚举
typedef enum {
    Err_success = 0, //成功
    Err_eof, //eof
    Err_timeout, //超时
    Err_refused,//连接被拒绝
    Err_dns,//dns解析失败
    Err_shutdown,//主动关闭
    Err_other = 0xFF,//其他错误
} ErrCode;

//错误信息类
class SockException : public std::exception {
public:
    SockException(ErrCode code = Err_success, const std::string& msg = "", int custom_code = 0);

    //重置错误
    void reset(ErrCode code, const std::string& msg, int custom_code = 0);

    //错误提示
    const char* what() const noexcept override {
        return _msg.c_str();
    }

    //错误代码
    ErrCode getErrCode() const {
        return _code;
    }

    //用户自定义错误代码
    int getCustomCode() const {
        return _custom_code;
    }

    //判断是否真的有错
    operator bool() const {
        return _code != Err_success;
    }

private:
    ErrCode _code;
    int _custom_code = 0;
    std::string _msg;
};

using EventPoller = hv::EventLoop;
typedef std::shared_ptr<EventPoller> EventPollerPtr;

class Timer {
	// return false to killtimer
	using TimeCB = std::function<bool()>;
	uint64_t _timer;
	TimeCB _cb;
	EventPoller* _loop;
public:
	using Ptr = std::shared_ptr<Timer>;
	Timer(float sec, TimeCB cb, std::shared_ptr<EventPoller> pool);
	void cancel();
	~Timer() {
		cancel();
	}
};

using SockInfo = hv::SocketChannel;
using Socket = hv::SocketChannel;
typedef std::shared_ptr<Socket> SocketPtr;

class Session;
typedef std::shared_ptr<Session> SessionPtr;

class Buffer;
typedef std::shared_ptr<Session> BufferPtr;

class Ticker;
typedef std::shared_ptr<Ticker> TickerPtr;
}

#define TraceP(ptr) TraceL << ptr->peeraddr() << " "
#define DebugP(ptr) DebugL << ptr->peeraddr() << " "
#define InfoP(ptr) InfoL << ptr->peeraddr() << " "
#define WarnP(ptr) WarnL << ptr->peeraddr() << " "
#define ErrorP(ptr) ErrorL << ptr->peeraddr() << " "

#endif
