#include "Session.h"
#include "EventLoop.h"
#include "TcpClient.hpp"
namespace toolkit {

SockException::SockException(ErrCode code /*= Err_success*/, const std::string& msg /*= ""*/, int custom_code /*= 0*/)
{
    _msg = msg;
    _code = code;
    _custom_code = custom_code;
}

void SockException::reset(ErrCode code, const std::string& msg, int custom_code /*= 0*/)
{
    _msg = msg;
    _code = code;
    _custom_code = custom_code;
}

Session::Session(hio_t* io) : hv::SocketChannel(io)
{
    setHeartbeat(2000, [this]() {
        onManager();
    });
    if (hio_getcb_close(io) == NULL) {
        hio_setcb_close(io_, [](hio_t* io) {
            Session* channel = (Session*)hio_context(io);
            if (channel) {
                channel->status = CLOSED;
                channel->onError(SockException(Err_eof, "socket closed"));
                if (channel->onclose) {
                    channel->onclose();
                }
            }
        });
    }
}

void Session::shutdown(const SockException& e, bool safe /*= false*/)
{
    hlogi("%p shutdown %s", this, e.what());
    if (!isClosed())
        onError(e);
    close(safe);
}

Timer::Timer(float sec, TimeCB cb, std::shared_ptr<EventPoller> pool) : _cb(cb) {
    _loop = pool ? pool.get() : hv::tlsEventLoop();
    _timer = _loop->setInterval(sec * 1000, [this](hv::TimerID tid) {
        if (!_cb()) {
            //hv::killTimer(tid);
            cancel();
        }
        });
}

void Timer::cancel() {
    if (_loop && _timer) {
        _loop->killTimer(_timer);
        _timer = 0;
    }
}

TcpClient::TcpClient(EventPollerPtr loop) : hv::TcpClient(loop) {
    onConnection = [this](hv::SocketChannelPtr channel){
        if (channel->isConnected()) {
            onConnect(SockException());
        } 
        else {
            onErr(SockException(Err_eof, "socket closed"));
        }
    };
    onMessage = [this](hv::SocketChannelPtr channel, hv::Buffer* buf) {
        auto buffer = BufferRaw::create();
        buffer->assign((char*)buf->data(), buf->size());
        onRecv(buffer);
    };
    onWriteComplete = [this](hv::SocketChannelPtr channel, hv::Buffer* buf) {
        // onFlush();
    }; 
    // setHeartbeat(2000, [this]() {onManager();});
}

void TcpClient::shutdown(const SockException &ex){
    //InfoL << ex.what();
    if (channel) {
        closesocket();
        onErr(ex);
    }
}
}

