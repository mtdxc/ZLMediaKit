﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_TRACK_H
#define ZLMEDIAKIT_TRACK_H

#include <memory>
#include <string>
#include "Frame.h"
// for Sdp
#include "Rtsp/Rtsp.h"

namespace mediakit{

/**
 * 媒体通道描述类，也支持帧输入输出
 */
class Track : public FrameDispatcher , public CodecInfo{
public:
    typedef std::shared_ptr<Track> Ptr;

    Track(){}
    virtual ~Track(){}

    /**
     * 是否准备好，准备好才能获取譬如sps pps等信息
     */
    virtual bool ready() = 0;

    /**
     * 克隆接口，用于复制本对象用
     * 在调用该接口时只会复制派生类的信息
     * 环形缓存和代理关系不能拷贝，否则会关系紊乱
     */
    virtual Track::Ptr clone() = 0;

    /**
     * 生成sdp
     * @return sdp对象
     */
    virtual Sdp::Ptr getSdp() = 0;

    /**
     * 返回比特率
     * @return 比特率
     */
    virtual int getBitRate() const { return _bit_rate; }

    /**
     * 设置比特率
     * @param bit_rate 比特率
     */
    virtual void setBitRate(int bit_rate) { _bit_rate = bit_rate; }

    /**
     * 复制拷贝，只能拷贝派生类的信息，
     * 环形缓存和代理关系不能拷贝，否则会关系紊乱
     */
    Track(const Track &that){
        _bit_rate = that._bit_rate;
    }

private:
    int _bit_rate = 0;
};

/**
 * 视频通道描述Track类，支持获取宽高fps信息
 */
class VideoTrack : public Track {
public:
    typedef std::shared_ptr<VideoTrack> Ptr;

    /**
     * 返回视频高度
     */
    virtual int getVideoHeight() const {return 0;};

    /**
     * 返回视频宽度
     */
    virtual int getVideoWidth() const {return 0;};

    /**
     * 返回视频fps
     */
    virtual float getVideoFps() const {return 0;};
};

/**
 * 音频Track派生类，支持采样率通道数，采用位数信息
 */
class AudioTrack : public Track {
public:
    typedef std::shared_ptr<AudioTrack> Ptr;

    /**
     * 返回音频采样率
     */
    virtual int getAudioSampleRate() const  {return 0;};

    /**
     * 返回音频采样位数，一般为16或8
     */
    virtual int getAudioSampleBit() const {return 0;};

    /**
     * 返回音频通道数
     */
    virtual int getAudioChannel() const {return 0;};
};

// Track容器类
class TrackSource{
public:
    TrackSource(){}
    virtual ~TrackSource(){}

    /**
     * 获取全部Track列表
     * @param trackReady 是否获取全部ready的Track
     */
    virtual std::vector<Track::Ptr> getTracks(bool trackReady = true) const = 0;

    /**
     * 获取特定类型的Track列表
     * @param type track类型
     * @param trackReady 是否获取全部已经准备好的Track
     */
    Track::Ptr getTrack(TrackType type , bool trackReady = true) const {
        auto tracks = getTracks(trackReady);
        for(auto &track : tracks){
            if(track->getTrackType() == type){
                return track;
            }
        }
        return nullptr;
    }
};

}//namespace mediakit
#endif //ZLMEDIAKIT_TRACK_H