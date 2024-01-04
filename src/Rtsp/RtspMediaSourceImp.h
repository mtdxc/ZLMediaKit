﻿/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SRC_RTSP_RTSPTORTMPMEDIASOURCE_H_
#define SRC_RTSP_RTSPTORTMPMEDIASOURCE_H_

#include "RtspMediaSource.h"
#include "Common/MultiMediaSourceMuxer.h"

namespace mediakit {
class RtspDemuxer;
class RtspMediaSourceImp : public RtspMediaSource, private TrackListener, public MultiMediaSourceMuxer::Listener {
public:
    using Ptr = std::shared_ptr<RtspMediaSourceImp>;

    /**
     * 构造函数
     * @param vhost 虚拟主机
     * @param app 应用名
     * @param id 流id
     * @param ringSize 环形缓存大小
     * Constructor
     * @param vhost Virtual host
     * @param app Application name
     * @param id Stream id
     * @param ringSize Ring buffer size
     
     * [AUTO-TRANSLATED:7679d212]
     */
    RtspMediaSourceImp(const MediaTuple& tuple, const std::string &schema = RTSP_SCHEMA, int ringSize = RTP_GOP_SIZE);

    /**
     * 设置sdp
     * Set sdp
     
     * [AUTO-TRANSLATED:76a533c4]
     */
    void setSdp(const std::string &strSdp) override;

    /**
     * 输入rtp并解析
     * Input rtp and parse
     
     * [AUTO-TRANSLATED:778f743f]
     */
    void onWrite(RtpPacket::Ptr rtp, bool key_pos) override;

    /**
     * 获取观看总人数，包括(hls/rtsp/rtmp)
     * Get total number of viewers, including (hls/rtsp/rtmp)
     
     * [AUTO-TRANSLATED:19a26d5a]
     */
    int totalReaderCount() override {
        return readerCount() + (_muxer ? _muxer->totalReaderCount() : 0);
    }

    /**
     * 设置协议转换选项
     * Set protocol conversion options
     
     * [AUTO-TRANSLATED:a6a9b24a]
     */
    void setProtocolOption(const ProtocolOption &option);

    const ProtocolOption &getProtocolOption() const {
        return _option;
    }

    /**
     * _demuxer触发的添加Track事件
     * _demuxer triggered add Track event
     
     * [AUTO-TRANSLATED:80dbcf16]
     */
    bool addTrack(const Track::Ptr &track) override;

    /**
     * _demuxer触发的Track添加完毕事件
     * _demuxer triggered Track add complete event
     
     * [AUTO-TRANSLATED:939cb312]
     */
    void addTrackCompleted() override;

    void resetTracks() override;

    /**
     * _muxer触发的所有Track就绪的事件
     * _muxer triggered all Track ready event
     
     * [AUTO-TRANSLATED:1d34b7e0]
     */
    void onAllTrackReady() override{
        _all_track_ready = true;
    }

    /**
     * 设置事件监听器
     * @param listener 监听器
     * Set event listener
     * @param listener Listener
     
     * [AUTO-TRANSLATED:d829419b]
     */
    void setListener(const std::weak_ptr<MediaSourceEvent> &listener) override;

    RtspMediaSource::Ptr clone(const std::string& stream) override;
protected:
    bool _all_track_ready = false;
    ProtocolOption _option;
    std::shared_ptr<RtspDemuxer> _demuxer;
    MultiMediaSourceMuxer::Ptr _muxer;
};
} /* namespace mediakit */

#endif /* SRC_RTSP_RTSPTORTMPMEDIASOURCE_H_ */
