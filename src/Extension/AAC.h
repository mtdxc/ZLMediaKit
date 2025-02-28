﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_AAC_H
#define ZLMEDIAKIT_AAC_H

#include "Frame.h"
#include "AudioTrack.h"
#define ADTS_HEADER_LEN 7

namespace mediakit{
// adts头部转audioSpecificConfig(2字节)
std::string makeAacConfig(const uint8_t *hex, size_t length);
// 从adts头部中获取长度
int getAacFrameLength(const uint8_t *hex, size_t length);
// 利用audioSpecificConfig和长度，生成ads头部
int dumpAacConfig(const std::string &config, size_t length, uint8_t *out, size_t out_size);
// 解析audioSpecificConfig获取采样率和通道
bool parseAacConfig(const std::string &config, int &samplerate, int &channels);

/**
 * aac音频通道
 */
class AACTrack : public AudioTrack{
public:
    using Ptr = std::shared_ptr<AACTrack>;

    /**
     * 延后获取adts头信息
     * 在随后的inputFrame中获取adts头信息
     */
    AACTrack() = default;

    /**
     * 构造aac类型的媒体
     * @param aac_cfg aac配置信息
     */
    AACTrack(const std::string &aac_cfg);

    /**
     * 获取aac 配置信息
     */
    const std::string &getAacCfg() const { return _cfg; }

    bool ready() override { return !_cfg.empty(); }

    CodecId getCodecId() const override { return CodecAAC; }
    int getAudioChannel() const override { return _channel; }
    int getAudioSampleRate() const override { return _sampleRate; }
    int getAudioSampleBit() const override { return _sampleBit; }

    bool inputFrame(const Frame::Ptr &frame) override;

private:
    void onReady();
    Sdp::Ptr getSdp() override;
    Track::Ptr clone() override;
    bool inputFrame_l(const Frame::Ptr &frame);

private:
    std::string _cfg;
    int _channel = 0;
    int _sampleRate = 0;
    int _sampleBit = 16;
};

}//namespace mediakit
#endif //ZLMEDIAKIT_AAC_H