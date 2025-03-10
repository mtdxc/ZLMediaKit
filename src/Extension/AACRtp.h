﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_AACRTPCODEC_H
#define ZLMEDIAKIT_AACRTPCODEC_H

#include "Rtsp/RtpCodec.h"
#include "Extension/AAC.h"
namespace mediakit{
/**
 * aac rtp转adts类
 */
class AACRtpDecoder : public RtpCodec {
public:
    typedef std::shared_ptr<AACRtpDecoder> Ptr;

    AACRtpDecoder(const Track::Ptr &track);
    ~AACRtpDecoder() {}

    /**
     * 输入rtp并解码
     * @param rtp rtp数据包
     * @param key_pos 此参数内部强制转换为false,请忽略之
     */
    bool inputRtp(const RtpPacket::Ptr &rtp, bool key_pos = false) override;

    CodecId getCodecId() const override {
        return CodecAAC;
    }

protected:
    AACRtpDecoder();

private:
    void obtainFrame();
    void flushData();

private:
    uint64_t _last_dts = 0;
    std::string _aac_cfg;
    FrameImp::Ptr _frame;
};


/**
 * aac adts转rtp类
 */
class AACRtpEncoder : public RtpCodec, public RtpInfo {
public:
    typedef std::shared_ptr<AACRtpEncoder> Ptr;
    CodecId getCodecId() const override {
        return CodecAAC;
    }

    AACRtpEncoder(uint32_t ssrc,
                  uint32_t mtuSize,
                  uint32_t sampleRate,                  ///< 采样率
                  uint8_t payloadType = 97,             ///< pt类型
                  uint8_t interleaved = TrackAudio * 2  ///< rtsp interleaved 值
                  );
                  
    ~AACRtpEncoder() {}

    /**
     * 输入aac 数据，必须带dats头
     * @param frame 带dats头的aac数据
     */
    bool inputFrame(const Frame::Ptr &frame) override;

private:
    void makeAACRtp(const void *data, size_t len, bool mark, uint64_t stamp);

private:
    unsigned char _section_buf[1600];
};

}//namespace mediakit

#endif //ZLMEDIAKIT_AACRTPCODEC_H
