﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "WebRtcPusher.h"
#include "Common/config.h"

using namespace std;

namespace mediakit {

WebRtcPusher::Ptr WebRtcPusher::create(const EventPoller::Ptr &poller,
                                       const RtspMediaSourceImp::Ptr &src,
                                       const std::shared_ptr<void> &ownership,
                                       const MediaInfo &info,
                                       const ProtocolOption &option,
                                       bool perferred_tcp) {
    WebRtcPusher::Ptr ret(new WebRtcPusher(poller, src, ownership, info, option,perferred_tcp), [](WebRtcPusher *ptr) {
        ptr->onDestory();
        delete ptr;
    });
    ret->onCreate();
    return ret;
}

WebRtcPusher::WebRtcPusher(const EventPoller::Ptr &poller,
                           const RtspMediaSourceImp::Ptr &src,
                           const std::shared_ptr<void> &ownership,
                           const MediaInfo &info,
                           const ProtocolOption &option,
                           bool perferred_tcp) : WebRtcTransportImp(poller,perferred_tcp) {
    _media_info = info;
    _push_src = src;
    _push_src_ownership = ownership;
    _continue_push_ms = option.continue_push_ms;
    CHECK(_push_src);
}

bool WebRtcPusher::close(MediaSource &sender) {
    //此回调在其他线程触发
    string err = StrPrinter << "close media: " << sender.getUrl();
    weak_ptr<WebRtcPusher> weak_self = static_pointer_cast<WebRtcPusher>(shared_from_this());
    getPoller()->async([weak_self, err]() {
        auto strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->onShutdown(SockException(Err_shutdown, err));
            //主动关闭推流，那么不延时注销
            strong_self->_push_src = nullptr;
        }
    });
    return true;
}

int WebRtcPusher::totalReaderCount(MediaSource &sender) {
    auto total_count = 0;
    for (auto &src : _push_src_sim) {
        total_count += src.second->totalReaderCount();
    }
    return total_count + _push_src->totalReaderCount();
}

MediaOriginType WebRtcPusher::getOriginType(MediaSource &sender) const {
    return MediaOriginType::rtc_push;
}

std::string WebRtcPusher::getOriginUrl(MediaSource &sender) const {
    return _media_info._full_url;
}

std::shared_ptr<SockInfo> WebRtcPusher::getOriginSock(MediaSource &sender) const {
    return std::static_pointer_cast<SockInfo>(getSession());
}

toolkit::EventPoller::Ptr WebRtcPusher::getOwnerPoller(MediaSource &sender) {
    return getPoller();
}

void WebRtcPusher::onRecvRtp(MediaTrack &track, const string &rid, RtpPacket::Ptr rtp) {
    if (!_simulcast) {
        assert(_push_src);
        _push_src->onWrite(rtp, false);
        return;
    }

    if (rtp->type == TrackAudio) {
        //音频
        for (auto &pr : _push_src_sim) {
            pr.second->onWrite(rtp, false);
        }
    } else {
        //视频
        auto &src = _push_src_sim[rid];
        if (!src) {
            auto stream_id = _push_src->getId();
            // 根据rid创建多个流
            if (rid.length()) stream_id = stream_id + "_" + rid;
            auto src_imp = std::make_shared<RtspMediaSourceImp>(_push_src->getVhost(), _push_src->getApp(), stream_id);
            _push_src_sim_ownership[rid] = src_imp->getOwnership();
            src_imp->setSdp(_push_src->getSdp());
            src_imp->setProtocolOption(_push_src->getProtocolOption());
            src_imp->setListener(std::static_pointer_cast<WebRtcPusher>(shared_from_this()));
            src = src_imp;
        }
        src->onWrite(std::move(rtp), false);
    }
}

void WebRtcPusher::onStartWebRTC() {
    WebRtcTransportImp::onStartWebRTC();
    _simulcast = _answer_sdp->supportSimulcast();
    if (canRecvRtp()) {
        _push_src->setSdp(_answer_sdp->toRtspSdp());
    }
}

void WebRtcPusher::onDestory() {
    WebRtcTransportImp::onDestory();

    auto duration = getDuration();
    auto bytes_usage = getBytesUsage();
    //流量统计事件广播
    GET_CONFIG(uint32_t, iFlowThreshold, General::kFlowThreshold);

    if (getSession()) {
        WarnL << "RTC推流器("
              << _media_info.shortUrl()
              << ")结束推流,耗时(s):" << duration;
        if (bytes_usage >= iFlowThreshold * 1024) {
            NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastFlowReport, _media_info, bytes_usage, duration,
                                               false, static_cast<SockInfo &>(*getSession()));
        }
    }

    if (_push_src && _continue_push_ms) {
        //取消所有权
        _push_src_ownership = nullptr;
        //延时10秒注销流
        auto push_src = std::move(_push_src);
        getPoller()->doDelayTask(_continue_push_ms, [push_src]() { return 0; });
    }
}

void WebRtcPusher::onRtcConfigure(RtcConfigure &configure) const {
    WebRtcTransportImp::onRtcConfigure(configure);
    //这只是推流
    configure.audio.direction = configure.video.direction = RtpDirection::recvonly;
}

float WebRtcPusher::getLossRate(MediaSource &sender,TrackType type){
    return WebRtcTransportImp::getLossRate(type);
}

void WebRtcPusher::OnDtlsTransportClosed(const RTC::DtlsTransport *dtlsTransport) {
   //主动关闭推流，那么不等待重推
    _push_src = nullptr;
    WebRtcTransportImp::OnDtlsTransportClosed(dtlsTransport);
}

void WebRtcPusher::onRtcpBye(){
     WebRtcTransportImp::onRtcpBye();
}

}// namespace mediakit