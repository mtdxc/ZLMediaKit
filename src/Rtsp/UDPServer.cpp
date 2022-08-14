/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "UDPServer.h"
#include "Util/TimeTicker.h"
#include "Util/onceToken.h"
#include "Util/logger.h"
#include "EventLoop.h"

using namespace toolkit;
using std::string;
using AutoLock = std::unique_lock<std::mutex>;

namespace mediakit {

INSTANCE_IMP(UDPServer);
    
UDPServer::UDPServer() {
}

UDPServer::~UDPServer() {
    InfoL;
}

Session::Ptr UDPServer::getSock(toolkit::EventPollerPtr loop, const char* local_ip, int interleaved, uint16_t local_port) {
    AutoLock lck(_mtx_udp_sock);
    string key = StrPrinter << local_ip << ":" << interleaved;
    auto it = _udp_sock_map.find(key);
    if (it == _udp_sock_map.end()) {
        hio_t* io = hio_create_socket(loop->loop(), local_ip, local_port, HIO_TYPE_UDP);
        auto sock = std::make_shared<toolkit::Session>(io);
        sock->onclose = [this, key]() //const SockException &err) 
        {
            //WarnL << err.what();
            AutoLock lck(_mtx_udp_sock);
            _udp_sock_map.erase(key);
        };
        sock->onread = [this, interleaved, io](hv::Buffer* buf) {
            char peer_ip[32];
            auto peer_addr = hio_peeraddr(io);
            sockaddr_ip((sockaddr_u*)peer_addr, peer_ip, sizeof(peer_ip));
            AutoLock lck(_mtx_on_recv);
            auto it0 = _on_recv_map.find(peer_ip);
            if (it0 == _on_recv_map.end()) {
                return;
            }
            auto buff = toolkit::BufferRaw::create();
            buff->assign((const char*)buf->data(), buf->size());
            auto &ref = it0->second;
            for (auto it1 = ref.begin(); it1 != ref.end(); ++it1) {
                auto &func = it1->second;
                if (!func(interleaved, buff, peer_addr)) {
                    it1 = ref.erase(it1);
                }
            }
            if (ref.size() == 0) {
                _on_recv_map.erase(it0);
            }
        };

        _udp_sock_map[key] = sock;
        DebugL << local_ip << " " << sock->get_local_port() << " " << interleaved;
        return sock;
    }
    return it->second;
}

void UDPServer::listenPeer(const char* peer_ip, void* obj, const onRecvData &cb) {
    AutoLock lck(_mtx_on_recv);
    auto &ref = _on_recv_map[peer_ip];
    ref.emplace(obj, cb);
}

void UDPServer::stopListenPeer(const char* peer_ip, void* obj) {
    AutoLock lck(_mtx_on_recv);
    auto it0 = _on_recv_map.find(peer_ip);
    if (it0 == _on_recv_map.end()) {
        return;
    }
    auto &ref = it0->second;
    auto it1 = ref.find(obj);
    if (it1 != ref.end()) {
        ref.erase(it1);
    }
    if (ref.size() == 0) {
        _on_recv_map.erase(it0);
    }
}

} /* namespace mediakit */


