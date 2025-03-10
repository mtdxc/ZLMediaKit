﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <list>
#include <type_traits>
#include "RtpMultiCaster.h"
#include "Util/util.h"
#include "Network/sockutil.h"
#include "RtspSession.h"
#include "Common/config.h"

using std::string;
using AutoLock = std::lock_guard<std::recursive_mutex>;
using namespace toolkit;

namespace mediakit{

MultiCastAddressMaker &MultiCastAddressMaker::Instance() {
    static MultiCastAddressMaker instance;
    return instance;
}

bool MultiCastAddressMaker::isMultiCastAddress(uint32_t addr) {
    GET_CONFIG(uint32_t, addrMin, MultiCast::kAddrMin);
    GET_CONFIG(uint32_t, addrMax, MultiCast::kAddrMax);
    return addr >= addrMin && addr <= addrMax;
}

string MultiCastAddressMaker::toString(uint32_t addr) {
    addr = htonl(addr);
    return SockUtil::inet_ntoa((struct in_addr &) (addr));
}

static uint32_t addressToInt(const string &ip){
    struct in_addr addr;
    bzero(&addr, sizeof(addr));
    addr.s_addr = inet_addr(ip.data());
    return (uint32_t) ntohl((uint32_t &) addr.s_addr);
}

std::shared_ptr<uint32_t> MultiCastAddressMaker::obtain(uint32_t max_try) {
    AutoLock lck(_mtx);
    GET_CONFIG_FUNC(uint32_t, addrMin, MultiCast::kAddrMin, [](const string &str) {
        return addressToInt(str);
    });
    GET_CONFIG_FUNC(uint32_t, addrMax, MultiCast::kAddrMax, [](const string &str) {
        return addressToInt(str);
    });
    for (int i = 0; i < max_try; i++) {
        if (_addr > addrMax || _addr == 0) {
            _addr = addrMin;
        }
        auto iGotAddr = _addr++;
        if (!_used_addr.count(iGotAddr)) {
            _used_addr.emplace(iGotAddr);
            return std::shared_ptr<uint32_t>(new uint32_t(iGotAddr), [](uint32_t *ptr) {
                MultiCastAddressMaker::Instance().release(*ptr);
                delete ptr;
            });
        }
    }
    //分配完了,应该不可能到这里
    ErrorL;
    return nullptr;
}

void MultiCastAddressMaker::release(uint32_t addr){
    AutoLock lck(_mtx);
    _used_addr.erase(addr);
}

////////////////////////////////////////////////////////////////////////////////////

std::recursive_mutex g_mtx;
std::unordered_map<string, std::weak_ptr<RtpMultiCaster> > g_multi_caster_map;

void RtpMultiCaster::setDetachCB(void* listener, const onDetach& cb) {
    AutoLock lck(_mtx);
    if (cb) {
        _detach_map.emplace(listener, cb);
    } else {
        _detach_map.erase(listener);
    }
}

RtpMultiCaster::~RtpMultiCaster() {
    _rtp_reader->setReadCB(nullptr);
    _rtp_reader->setDetachCB(nullptr);
    DebugL;
}

RtpMultiCaster::RtpMultiCaster(SocketHelper &helper, const string &local_ip, const string &vhost, const string &app, const string &stream) {
    auto src = std::dynamic_pointer_cast<RtspMediaSource>(MediaSource::find(RTSP_SCHEMA, vhost, app, stream));
    if (!src) {
        throw std::runtime_error(StrPrinter << "未找到媒体源:" << vhost << " " << app << " " << stream);
    }
    _multicast_ip = MultiCastAddressMaker::Instance().obtain();
    if (!_multicast_ip) {
        throw std::runtime_error("获取组播地址失败");
    }

    for (auto i = 0; i < 2; ++i) {
        //创建udp socket, 数组下标为TrackType
        _udp_sock[i] = helper.createSocket();
        if (!_udp_sock[i]->bindUdpSock(0, local_ip.data())) {
            throw std::runtime_error(StrPrinter << "绑定UDP端口失败:" << local_ip);
        }
        auto fd = _udp_sock[i]->rawFD();
        GET_CONFIG(uint32_t, udpTTL, MultiCast::kUdpTTL);
        SockUtil::setMultiTTL(fd, udpTTL);
        SockUtil::setMultiLOOP(fd, false);
        SockUtil::setMultiIF(fd, local_ip.data());

        struct sockaddr_in peer;
        peer.sin_family = AF_INET;
        //组播目标端口为本地发送端口
        peer.sin_port = htons(_udp_sock[i]->get_local_port());
        //组播目标地址
        peer.sin_addr.s_addr = htonl(*_multicast_ip);
        bzero(&(peer.sin_zero), sizeof peer.sin_zero);
        _udp_sock[i]->bindPeerAddr((struct sockaddr *) &peer);
    }

    src->pause(false);
    _rtp_reader = src->getRing()->attach(helper.getPoller());
    _rtp_reader->setReadCB([this](const RtspMediaSource::RingDataType &pkt) {
        size_t i = 0;
        auto size = pkt->size();
        pkt->for_each([&](const RtpPacket::Ptr &rtp) {
            auto &sock = _udp_sock[rtp->type];
            // 略过4字节rtp over tcp头部
            sock->send(std::make_shared<BufferRtp>(rtp, 4), nullptr, 0, ++i == size);
        });
    });

    _rtp_reader->setDetachCB([this]() {
        decltype(_detach_map) _detach_map_copy;
        {
            AutoLock lck(_mtx);
            _detach_map_copy = std::move(_detach_map);
        }
        for (auto &pr : _detach_map_copy) {
            pr.second();
        }
    });

    DebugL << MultiCastAddressMaker::toString(*_multicast_ip) << " "
           << _udp_sock[0]->get_local_port() << " "
           << _udp_sock[1]->get_local_port() << " "
           << vhost << " " << app << " " << stream;
}

uint16_t RtpMultiCaster::getMultiCasterPort(TrackType trackType) {
    return _udp_sock[trackType]->get_local_port();
}

string RtpMultiCaster::getMultiCasterIP() {
    struct in_addr addr;
    addr.s_addr = htonl(*_multicast_ip);
    return SockUtil::inet_ntoa(addr);
}

RtpMultiCaster::Ptr RtpMultiCaster::get(SocketHelper &helper, const string &local_ip, const string &vhost, const string &app, const string &stream) {
    static auto on_create = [](SocketHelper &helper, const string &local_ip, const string &vhost, const string &app, const string &stream){
        try {
            auto poller = helper.getPoller();
            auto ret = RtpMultiCaster::Ptr(new RtpMultiCaster(helper, local_ip, vhost, app, stream), [poller](RtpMultiCaster *ptr) {
                poller->async([ptr]() {
                    delete ptr;
                });
            });
            // regist myself with name
            AutoLock lck(g_mtx);
            string strKey = StrPrinter << local_ip << " " << vhost << " " << app << " " << stream;
            g_multi_caster_map.emplace(strKey, ret);
            return ret;
        } catch (std::exception &ex) {
            WarnL << ex.what();
            return RtpMultiCaster::Ptr();
        }
    };

    string strKey = StrPrinter << local_ip << " " << vhost << " " << app << " " << stream;
    AutoLock lck(g_mtx);
    auto it = g_multi_caster_map.find(strKey);
    if (it == g_multi_caster_map.end()) {
        return on_create(helper, local_ip, vhost, app, stream);
    }
    auto ret = it->second.lock();
    if (!ret) {
        g_multi_caster_map.erase(it);
        return on_create(helper, local_ip, vhost, app, stream);
    }
    return ret;
}


}//namespace mediakit
