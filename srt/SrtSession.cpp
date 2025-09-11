#include "SrtSession.hpp"
#include "Packet.hpp"
#include "SrtTransportImp.hpp"

#include "Common/config.h"

namespace SRT {
using namespace mediakit;

SrtSession::SrtSession(hio_t* io) : Session(io) {
    socklen_t addr_len = sizeof(_peer_addr);
    // TraceL<<"before addr len "<<addr_len;
    getpeername(hio_fd(io), (struct sockaddr *)&_peer_addr, &addr_len);
    // TraceL<<"after addr len "<<addr_len<<" family "<<_peer_addr.ss_family;
    so_rcvbuf(fd(), 1024 * 1024);
}

extern SrtTransport::Ptr querySrtTransport(uint8_t *data, size_t size, const EventPoller::Ptr& poller);

EventPoller::Ptr SrtSession::queryPoller(uint8_t *data, size_t size) {
    auto transport = querySrtTransport(data, size, nullptr);
    return transport ? transport->getPoller() : nullptr;
}

void SrtSession::onRecv(uint8_t * data, size_t size) {
    if (_find_transport) {
        //只允许寻找一次transport
        _find_transport = false;
        _transport = querySrtTransport(data, size, getPoller());
        if (_transport) {
            _transport->setSession(std::static_pointer_cast<Session>(shared_from_this()));
        }
        InfoP(this);
    }
    _ticker.resetTime();

    if (_transport) {
        _transport->inputSockData(data, size, &_peer_addr);
    } else {
        // WarnL<< "ingore  data";
    }
}

void SrtSession::onError(const SockException &err) {
    // udp链接超时，但是srt链接不一定超时，因为可能存在udp链接迁移的情况
    // 在udp链接迁移时，新的SrtSession对象将接管SrtSession对象的生命周期
    WarnP(this) << err;

    if (!_transport) {
        return;
    }

    // 防止互相引用导致不释放
    auto transport = std::move(_transport);
    getPoller()->async(
        [transport] {
            //延时减引用，防止使用transport对象时，销毁对象
            //transport->onShutdown(err);
        },
        false);
}

void SrtSession::onManager() {
    GET_CONFIG(float, timeoutSec, kTimeOutSec);
    if (_ticker.elapsedTime() > timeoutSec * 1000) {
        shutdown(SockException(Err_timeout, "srt connection timeout"));
        return;
    }
}

} // namespace SRT