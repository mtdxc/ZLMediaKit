﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
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
     */
    RtspMediaSourceImp(const std::string &vhost, const std::string &app, const std::string &id, const std::string &schema = RTSP_SCHEMA, int ringSize = RTP_GOP_SIZE);

    ~RtspMediaSourceImp() override = default;

    /**
     * 设置sdp
     */
    void setSdp(const std::string &strSdp) override;

    /**
     * 输入rtp并解析
     */
    void onWrite(RtpPacket::Ptr rtp, bool key_pos) override;

    /**
     * 获取观看总人数，包括(hls/rtsp/rtmp)
     */
    int totalReaderCount() override {
        return readerCount() + (_muxer ? _muxer->totalReaderCount() : 0);
    }

    /**
     * 设置协议转换选项
     */
    void setProtocolOption(const ProtocolOption &option);

    const ProtocolOption &getProtocolOption() const {
        return _option;
    }

    /**
     * _demuxer触发的添加Track事件
     */
    bool addTrack(const Track::Ptr &track) override;

    /**
     * _demuxer触发的Track添加完毕事件
     */
    void addTrackCompleted() override;

    void resetTracks() override;

    /**
     * _muxer触发的所有Track就绪的事件
     */
    void onAllTrackReady() override{
        _all_track_ready = true;
    }

    /**
     * 设置事件监听器
     * @param listener 监听器
     */
    void setListener(const std::weak_ptr<MediaSourceEvent> &listener) override;

protected:
    bool _all_track_ready = false;
    ProtocolOption _option;
    std::shared_ptr<RtspDemuxer> _demuxer;
    MultiMediaSourceMuxer::Ptr _muxer;
};
} /* namespace mediakit */

#endif /* SRC_RTSP_RTSPTORTMPMEDIASOURCE_H_ */
