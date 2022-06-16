/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "CommonRtmp.h"

namespace mediakit{

CommonRtmpDecoder::CommonRtmpDecoder(CodecId codec) {
    _codec = codec;
}

void CommonRtmpDecoder::inputRtmp(const RtmpPacket::Ptr &rtmp) {
    auto frame = FrameImp::create();
    frame->_codec_id = _codec;
    //拷贝负载
    frame->_buffer.assign(rtmp->buffer.data() + 1, rtmp->buffer.size() - 1);
    frame->_dts = rtmp->time_stamp;
    //写入环形缓存
    RtmpCodec::inputFrame(frame);
}

/////////////////////////////////////////////////////////////////////////////////////

CommonRtmpEncoder::CommonRtmpEncoder(const Track::Ptr &track) {
    _codec = track->getCodecId();
    _audio_flv_flags = getAudioRtmpFlags(track);
}

bool CommonRtmpEncoder::inputFrame(const Frame::Ptr &frame) {
    if (!_audio_flv_flags) {
        return false;
    }
    auto rtmp = RtmpPacket::create();
    //header
    rtmp->buffer.push_back(_audio_flv_flags);
    //data
    rtmp->buffer.append(frame->data() + frame->prefixSize(), frame->size() - frame->prefixSize());
    rtmp->body_size = rtmp->buffer.size();
    rtmp->type_id = MSG_AUDIO;
    rtmp->chunk_id = CHUNK_AUDIO;
    rtmp->stream_index = STREAM_MEDIA;
    rtmp->time_stamp = frame->dts();
    RtmpCodec::inputRtmp(rtmp);
    return true;
}

}//namespace mediakit