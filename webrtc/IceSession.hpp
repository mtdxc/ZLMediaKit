/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */


#ifndef ZLMEDIAKIT_WEBRTC_ICE_SESSION_H
#define ZLMEDIAKIT_WEBRTC_ICE_SESSION_H

#include "Network/Session.h"
#include "IceTransport.hpp"
#include <string>

namespace mediakit {
class TurnSplit {
    std::string buffer_;
    int pduLen() const;

public:
    void clear() { buffer_.clear(); }
    bool input(const void *data, int len) { 
        buffer_.append((const char*)data, len);
        return pduLen() > 0;
    }
    toolkit::Buffer::Ptr nextPdu() { 
      int len = pduLen();
      if (len) {
          auto ret = toolkit::BufferRaw::create(len);
          ret->assign(buffer_.data(), len);
          buffer_.erase(0, len);
          return ret;
      }
      return nullptr;
    }
};

// 建议改成TurnSession，负责封装turn tcp和udp会话，与IceServer合在一起使用
class IceSession : public toolkit::Session, public RTC::IceTransport::Listener {
public:
    using Ptr = std::shared_ptr<IceSession>;
    using WeakPtr = std::weak_ptr<IceSession>;

    IceSession(const toolkit::Socket::Ptr &sock);
    ~IceSession() override;

    //// Session override////
    // void attachServer(const Server &server) override;
    void onRecv(const toolkit::Buffer::Ptr &) override;
    void onError(const toolkit::SockException &err) override;
    void onManager() override;

    // ice related callbacks ///
    void onIceTransportRecvData(const toolkit::Buffer::Ptr& buffer, const RTC::IceTransport::Pair::Ptr& pair) override;
    void onIceTransportGatheringCandidate(const RTC::IceTransport::Pair::Ptr& pair, const RTC::CandidateInfo& candidate) override;
    void onIceTransportDisconnected() override;
    void onIceTransportCompleted() override;

    void onRecv_l(const char *data, size_t len);
protected:
    bool _over_tcp = false;
    TurnSplit _tcp_split;
    RTC::IceTransport::Pair::Ptr _session_pair = nullptr;
    RTC::IceServer::Ptr _ice_transport;
};

}// namespace mediakit

#endif //ZLMEDIAKIT_WEBRTC_ICE_SESSION_H
