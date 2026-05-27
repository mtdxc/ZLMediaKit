/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "IceSession.hpp"
#include "Util/util.h"
#include "Util/Byte.hpp"
#include "Common/config.h"
#include "WebRtcTransport.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

////////////  IceSession //////////////////////////
IceSession::IceSession(const Socket::Ptr &sock) : Session(sock) {
    TraceL << getIdentifier();
    _over_tcp = sock->sockType() == SockNum::Sock_TCP;
    GET_CONFIG(string, iceUfrag, Rtc::kIceUfrag);
    GET_CONFIG(string, icePwd, Rtc::kIcePwd);
    _ice_transport = std::make_shared<IceServer>(this, iceUfrag, icePwd, getPoller());
    _ice_transport->initialize();
}

IceSession::~IceSession() {
    TraceL << getIdentifier();
}

void IceSession::onRecv(const Buffer::Ptr &buffer) {
    // TraceL;
    if (_over_tcp) {
        if (_tcp_split.input(buffer->data(), buffer->size())) {
            while (auto pkt = _tcp_split.nextPdu()) {
                onRecv_l(pkt->data(), pkt->size());
            }
        }
    }
    else{
        onRecv_l(buffer->data(), buffer->size());
    }
}

void IceSession::onRecv_l(const char* buffer, size_t size) {
    if (!_session_pair) {
        auto relayed_addr = std::make_shared<sockaddr_storage>();
        memcpy(relayed_addr.get(), this->get_peer_addr(), sizeof(sockaddr_storage));
        _session_pair = std::make_shared<IceTransport::Pair>(shared_from_this(), 
          get_peer_ip(), get_peer_port(), relayed_addr);
    }
    _ice_transport->processSocketData((const uint8_t *)buffer, size, _session_pair);
}

void IceSession::onError(const SockException &err) {
    InfoL;
    // 消除循环引用
    _session_pair = nullptr;
}

void IceSession::onManager() {
}

void IceSession::onIceTransportRecvData(const toolkit::Buffer::Ptr& buffer, const IceTransport::Pair::Ptr& pair) {
    _ice_transport->processSocketData((const uint8_t *)buffer->data(), buffer->size(), pair);
}

void IceSession::onIceTransportGatheringCandidate(const IceTransport::Pair::Ptr& pair, const CandidateInfo& candidate) {
    DebugL << candidate.dumpString();
}

void IceSession::onIceTransportDisconnected() {
    InfoL << getIdentifier();
}

void IceSession::onIceTransportCompleted() {
    InfoL << getIdentifier();
}

int TurnSplit::pduLen() const {
    if (buffer_.size() < 4)
        return 0;

    auto buff = (const uint8_t *)buffer_.data();
    uint16_t length = Byte::Get2Bytes(buff, 2);
    if ((buff[0] & 0xC0) == 0) { // StunPacket::isStun
        length += 20;
        if (buffer_.length() >= length) {
            return length;
        }
    } else if (buff[0] >= 0x40 && buff[0] <= 0x7F) { // channel data
        length = 4 + Byte::PadTo4Bytes(length);
        if (buffer_.length() >= length) {
            return length;
        }
    }
    return 0;
}

} // namespace mediakit
