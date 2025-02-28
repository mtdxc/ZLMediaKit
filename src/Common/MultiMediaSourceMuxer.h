﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_MULTIMEDIASOURCEMUXER_H
#define ZLMEDIAKIT_MULTIMEDIASOURCEMUXER_H

#include "Common/Stamp.h"
#include "Common/MediaSource.h"
#include "Common/MediaSink.h"
#include "Record/Recorder.h"
namespace mediakit {
class HlsRecorder;
class RtspMediaSourceMuxer;
class RtmpMediaSourceMuxer;
class TSMediaSourceMuxer;
class FMP4MediaSourceMuxer;
class RtpSender;


class MultiMediaSourceMuxer : public MediaSourceEventInterceptor, public MediaSink, public std::enable_shared_from_this<MultiMediaSourceMuxer>{
public:
    typedef std::shared_ptr<MultiMediaSourceMuxer> Ptr;

    class Listener {
    public:
        Listener() = default;
        virtual ~Listener() = default;
        virtual void onAllTrackReady() = 0;
    };

    MultiMediaSourceMuxer(const std::string &vhost, const std::string &app, const std::string &stream, float dur_sec = 0.0,const ProtocolOption &option = ProtocolOption());
    ~MultiMediaSourceMuxer() override = default;

    /**
     * 设置事件监听器
     * @param listener 监听器
     */
    void setMediaListener(const std::weak_ptr<MediaSourceEvent> &listener);

     /**
      * 随着Track就绪事件监听器
      * @param listener 事件监听器
     */
    void setTrackListener(const std::weak_ptr<Listener> &listener);

    /**
     * 返回总的消费者个数
     */
    int totalReaderCount() const;

    /**
     * 判断是否生效(是否正在转其他协议)
     */
    bool isEnabled();

    /**
     * 设置MediaSource时间戳
     * @param stamp 时间戳
     */
    void setTimeStamp(uint32_t stamp);

    /**
     * 重置track
     */
    void resetTracks() override;

    /////////////////////////////////MediaSourceEvent override/////////////////////////////////

    /**
     * 观看总人数
     * @param sender 事件发送者
     * @return 观看总人数
     */
    int totalReaderCount(MediaSource &sender) override;

    /**
     * 设置录制状态
     * @param type 录制类型
     * @param start 开始或停止
     * @param custom_path 开启录制时，指定自定义路径
     * @return 是否设置成功
     */
    bool setupRecord(MediaSource &sender, Recorder::type type, bool start, const std::string &custom_path, size_t max_second) override;

    /**
     * 获取录制状态
     * @param type 录制类型
     * @return 录制状态
     */
    bool isRecording(MediaSource &sender, Recorder::type type) override;

    /**
     * 开始发送ps-rtp流
     * @param dst_url 目标ip或域名
     * @param dst_port 目标端口
     * @param ssrc rtp的ssrc
     * @param is_udp 是否为udp
     * @param cb 启动成功或失败回调
     */
    void startSendRtp(MediaSource &sender, const MediaSourceEvent::SendRtpArgs &args, const std::function<void(uint16_t, const toolkit::SockException &)> cb) override;

    /**
     * 停止ps-rtp发送
     * @return 是否成功
     */
    bool stopSendRtp(MediaSource &sender, const std::string &ssrc) override;

    /**
     * 获取所有Track
     * @param trackReady 是否筛选过滤未就绪的track
     * @return 所有Track
     */
    std::vector<Track::Ptr> getMediaTracks(MediaSource &sender, bool trackReady = true) const override;

    /**
     * 获取所属线程
     */
    toolkit::EventPoller::Ptr getOwnerPoller(MediaSource &sender) override;

    const std::string& getVhost() const;
    const std::string& getApp() const;
    const std::string& getStreamId() const;
    std::string shortUrl() const;

protected:
    /////////////////////////////////MediaSink override/////////////////////////////////

    /**
    * 某track已经准备好，其ready()状态返回true，
    * 此时代表可以获取其例如sps pps等相关信息了
    * @param track
    */
    bool onTrackReady(const Track::Ptr & track) override;

    /**
     * 所有Track已经准备好，
     */
    void onAllTrackReady() override;

    /**
     * 某Track输出frame，在onAllTrackReady触发后才会调用此方法
     * @param frame
     */
    bool onTrackFrame(const Frame::Ptr &frame) override;

private:
    bool _is_enable = false;
    bool _create_in_poller = false;
    std::string _vhost;
    std::string _app;
    std::string _stream_id;
    ProtocolOption _option;
    toolkit::Ticker _last_check;
    Stamp _stamp[2];
    std::weak_ptr<Listener> _track_listener;
#if defined(ENABLE_RTPPROXY)
    std::unordered_map<std::string, std::shared_ptr<RtpSender>> _rtp_sender;
#endif //ENABLE_RTPPROXY

#if defined(ENABLE_MP4)
    std::shared_ptr<FMP4MediaSourceMuxer> _fmp4;
#endif
    std::shared_ptr<RtmpMediaSourceMuxer> _rtmp;
    std::shared_ptr<RtspMediaSourceMuxer> _rtsp;
    std::shared_ptr<TSMediaSourceMuxer> _ts;
    MediaSinkInterface::Ptr _mp4;
    std::shared_ptr<HlsRecorder> _hls;
    toolkit::EventPoller::Ptr _poller;

    //对象个数统计
    toolkit::ObjectStatistic<MultiMediaSourceMuxer> _statistic;
};

}//namespace mediakit
#endif //ZLMEDIAKIT_MULTIMEDIASOURCEMUXER_H
