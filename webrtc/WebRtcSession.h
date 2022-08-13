/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */


#ifndef ZLMEDIAKIT_WEBRTCSESSION_H
#define ZLMEDIAKIT_WEBRTCSESSION_H

#include "Session.h"
#include "Util/TimeTicker.h"
#include "Util/HttpRequestSplitter.h"

namespace toolkit {
    class TcpServer;
}

namespace mediakit {
class WebRtcTransportImp;
class WebRtcSession : public toolkit::Session, public HttpRequestSplitter {
public:
    WebRtcSession(hio_t* io);
    ~WebRtcSession() override;

    //void attachServer(const Server &server) override;
    void onRecv(hv::Buffer* buf);
    void onError(const toolkit::SockException &err);
    void onManager();
    std::string getIdentifier() const {
        return _identifier;
    }
    static std::string getUserName(void* buf, int len);
private:
    //// HttpRequestSplitter override ////
    ssize_t onRecvHeader(const char *data, size_t len) override;
    const char *onSearchPacketTail(const char *data, size_t len) override;

    void onRecv_l(const char *data, size_t len);
private:
    bool _over_tcp = false;
    bool _find_transport = true;
    toolkit::Ticker _ticker;
    struct sockaddr_storage _peer_addr;
    std::weak_ptr<toolkit::TcpServer> _server;
    std::shared_ptr<WebRtcTransportImp> _transport;
    std::string _identifier;
};

}// namespace mediakit

#endif //ZLMEDIAKIT_WEBRTCSESSION_H
