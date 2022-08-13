/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "WebRtcSession.h"
#include "WebRtcServer.h"
#include "StunPacket.hpp"
#include "Common/config.h"
#include "WebRtcTransport.h"
#include "IceServer.hpp"
#include "WebRtcTransport.h"

using namespace toolkit;

namespace mediakit {

std::string WebRtcSession::getUserName(void* buf, int len) {
    if (!RTC::StunPacket::IsStun((const uint8_t *) buf, len)) {
        return "";
    }
    std::unique_ptr<RTC::StunPacket> packet(RTC::StunPacket::Parse((const uint8_t *) buf, len));
    if (!packet) {
        return "";
    }
    if (packet->GetClass() != RTC::StunPacket::Class::REQUEST ||
        packet->GetMethod() != RTC::StunPacket::Method::BINDING) {
        return "";
    }
    //收到binding request请求
    auto vec = hv::split(packet->GetUsername(), ':');
    return vec[0];
}

WebRtcSession::WebRtcSession(hio_t* io) : Session(io)
{
    auto addr = hio_peeraddr(io);
    memcpy(&_peer_addr, addr, SOCKADDR_LEN(addr));
    _over_tcp = hio_type(io) == HIO_TYPE_TCP;
}


WebRtcSession::~WebRtcSession() {
    InfoP(this);
}
void WebRtcSession::onRecv_l(const char *data, size_t len) {
    if (_find_transport) {
        auto user_name = getUserName((void*)data, len);
        _identifier = std::to_string(fd()) + '-' + user_name;
        auto transport = WebRtcServer::Instance().getItem(user_name);
        CHECK(transport);
        if (!transport->getPoller()->isInLoopThread()) {
            hio_del(io_);
            hio_detach(io_);
            InfoP(this) << "transfer to loop " << transport->getPoller()->tid();
            std::string str(data, len);
            transport->getPoller()->async([this, str]() {
                hio_attach(hv::tlsEventLoop()->loop(), io_);
                this->startRead();
                this->onRecv_l(str.data(), str.size());
            });
            return ;
        }
        transport->setSession(shared_from_this());
        _transport = std::move(transport);
        // 只允许寻找一次transport
        _find_transport = false;
        //InfoP(this);
    }
    _ticker.resetTime();
    CHECK(_transport);
    _transport->inputSockData((char *)data, len, (struct sockaddr*)&_peer_addr);
}

void WebRtcSession::onRecv(hv::Buffer* buffer) {
    const char* data = (const char*)buffer->data();
    if (_over_tcp) {
        input(data, buffer->size());
    } else {
        onRecv_l(data, buffer->size());
    }
}

void WebRtcSession::onError(const SockException &err) {
    //udp链接超时，但是rtc链接不一定超时，因为可能存在链接迁移的情况
    //在udp链接迁移时，新的WebRtcSession对象将接管WebRtcTransport对象的生命周期
    //本WebRtcSession对象将在超时后自动销毁
    WarnP(this) << err.what();

    if (!_transport) {
        return;
    }
    auto transport = std::move(_transport);
    getPoller()->async([transport] {
        //延时减引用，防止使用transport对象时，销毁对象
    }, false);
}

void WebRtcSession::onManager() {
    GET_CONFIG(float, timeoutSec, Rtc::kTimeOutSec);
    if (!_transport && _ticker.createdTime() > timeoutSec * 1000) {
        shutdown(SockException(Err_timeout, "illegal webrtc connection"));
        return;
    }
    if (_ticker.elapsedTime() > timeoutSec * 1000) {
        shutdown(SockException(Err_timeout, "webrtc connection timeout"));
        return;
    }
}

ssize_t WebRtcSession::onRecvHeader(const char *data, size_t len) {
    onRecv_l(data + 2, len - 2);
    return 0;
}

const char *WebRtcSession::onSearchPacketTail(const char *data, size_t len) {
    if (len < 2) {
        // 数据不够
        return nullptr;
    }
    uint16_t length = (((uint8_t *)data)[0] << 8) | ((uint8_t *)data)[1];
    if (len < (size_t)(length + 2)) {
        // 数据不够
        return nullptr;
    }
    // 返回rtp包末尾
    return data + 2 + length;
}

}// namespace mediakit


