/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Rtmp/utils.h"
#include "H265Rtmp.h"
#include "Factory.h"
#include "Util/logger.h"

using std::string;
using namespace toolkit;

namespace mediakit{

H265RtmpDecoder::H265RtmpDecoder() {
}

/**
 * 返回不带0x00 00 00 01头的sps
 * @return
 */
static bool getH265ConfigFrame(const RtmpPacket &thiz, string &frame) {
    if (thiz.getMediaType() != FLV_CODEC_H265) {
        return false;
    }
    if (!thiz.isCfgFrame()) {
        return false;
    }
    if (thiz.buffer.size() < 28) {
        WarnL << "bad H265 cfg!";
        return false;
    }

    frame.clear();
    const char startcode[] = { 0, 0, 0, 1 };

    auto extra = (uint8_t*)thiz.buffer.data() + 5;
    auto bytes = thiz.buffer.size() - 5;
    auto end = extra + bytes;

    uint8_t numOfArrays = extra[22];
    uint8_t* p = extra + 23;
    for (int i = 0; i < numOfArrays; i++)
    {
        if (p + 3 > end)
            return false;

        uint8_t nalutype = p[0];
        uint16_t n = load_be16(p + 1);
        p += 3;

        for (int j = 0; j < n; j++)
        {
            if (p + 2 > end)
                return false;

            uint16_t k = load_be16(p);
            if (p + 2 + k > end)
            {
                assert(0);
                return false;
            }

            assert((nalutype & 0x3F) == ((p[2] >> 1) & 0x3F));
            /*
            hevc->nalu[hevc->numOfArrays].array_completeness = (nalutype >> 7) & 0x01;
            hevc->nalu[hevc->numOfArrays].type = nalutype & 0x3F;
            hevc->nalu[hevc->numOfArrays].bytes = k;
            hevc->nalu[hevc->numOfArrays].data = dst;
            memcpy(hevc->nalu[hevc->numOfArrays].data, p + 2, k);
            hevc->numOfArrays++;
            */
            frame.append(startcode, 4);
            frame.append((char*)p+2, k);
            p += 2 + k;
            // dst += k;
        }
    }
    return true;
}

void H265RtmpDecoder::inputRtmp(const RtmpPacket::Ptr &pkt) {
    if (pkt->isCfgFrame()) {
        string config;
        if (getH265ConfigFrame(*pkt, config) && config.size()>4) {
            onGetH265(config.data() + 4, config.size() - 4, pkt->time_stamp , pkt->time_stamp);
        }
        return;
    }

    if (pkt->buffer.size() > 9) {
        auto total_len = pkt->buffer.size();
        size_t offset = 5;
        uint8_t *cts_ptr = (uint8_t *) (pkt->buffer.data() + 2);
        int32_t cts = (load_be24(cts_ptr) + 0xff800000) ^ 0xff800000;
        auto pts = pkt->time_stamp + cts;
        while (offset + 4 < total_len) {
            uint32_t frame_len = load_be32(pkt->buffer.data() + offset);
            offset += 4;
            if (frame_len + offset > total_len) {
                break;
            }
            onGetH265(pkt->buffer.data() + offset, frame_len, pkt->time_stamp, pts);
            offset += frame_len;
        }
    }
}

inline void H265RtmpDecoder::onGetH265(const char* pcData, size_t iLen, uint32_t dts,uint32_t pts) {
    if(iLen == 0){
        return;
    }
#if 1
    auto frame = FrameImp::create<H265Frame>();
    frame->_dts = dts;
    frame->_pts = pts;
    frame->_buffer.assign("\x00\x00\x00\x01", 4);  //添加265头
    frame->_prefix_size = 4;
    frame->_buffer.append(pcData, iLen);

    //写入环形缓存
    RtmpCodec::inputFrame(frame);
#else
    //防止内存拷贝，这样产生的265帧不会有0x00 00 01头
    auto frame = std::make_shared<H265FrameNoCacheAble>((char *)pcData, iLen, dts, pts, 0);
    RtmpCodec::inputFrame(frame);
#endif
}

////////////////////////////////////////////////////////////////////////

H265RtmpEncoder::H265RtmpEncoder(const Track::Ptr &track) {
    _track = std::dynamic_pointer_cast<H265Track>(track);
}

RtmpPacket::Ptr H265RtmpEncoder::makeConfigPacket(){
    if (_track && _track->ready()) {
        //尝试从track中获取sps pps信息
        _sps = _track->getSps();
        _pps = _track->getPps();
        _vps = _track->getVps();
    }
    RtmpPacket::Ptr ret;
    if (!_sps.empty() && !_pps.empty() && !_vps.empty()) {
        //获取到sps/pps
        ret = makeVideoConfigPkt();
        _got_config_frame = true;
    }
    return ret;
}

void H265RtmpEncoder::flush() {
    inputFrame(nullptr);
}

bool H265RtmpEncoder::inputFrame(const Frame::Ptr &frame) {
    if (frame) {
        auto data = frame->data() + frame->prefixSize();
        auto len = frame->size() - frame->prefixSize();
        auto type = H265_TYPE(data[0]);
        switch (type) {
            case H265Frame::NAL_SPS: {
                if (!_got_config_frame) {
                    _sps = string(data, len);
                    makeConfigPacket();
                }
                break;
            }
            case H265Frame::NAL_PPS: {
                if (!_got_config_frame) {
                    _pps = string(data, len);
                    makeConfigPacket();
                }
                break;
            }
            case H265Frame::NAL_VPS: {
                if (!_got_config_frame) {
                    _vps = string(data, len);
                    makeConfigPacket();
                }
                break;
            }
            default: break;
        }
    }

    if (!_rtmp_packet) {
        _rtmp_packet = RtmpPacket::create();
        //flags/not_config/cts预占位
        _rtmp_packet->buffer.resize(5);
    }

    return _merger.inputFrame(frame, [this](uint64_t dts, uint64_t pts, const Buffer::Ptr &, bool have_key_frame) {
        //flags
        _rtmp_packet->buffer[0] = FLV_CODEC_H265 | ((have_key_frame ? FLV_KEY_FRAME : FLV_INTER_FRAME) << 4);
        //not config
        _rtmp_packet->buffer[1] = true;
        int32_t cts = pts - dts;
        if (cts < 0) {
            cts = 0;
        }
        //cts
        set_be24(&_rtmp_packet->buffer[2], cts);

        _rtmp_packet->time_stamp = dts;
        _rtmp_packet->body_size = _rtmp_packet->buffer.size();
        _rtmp_packet->chunk_id = CHUNK_VIDEO;
        _rtmp_packet->stream_index = STREAM_MEDIA;
        _rtmp_packet->type_id = MSG_VIDEO;
        //输出rtmp packet
        RtmpCodec::inputRtmp(_rtmp_packet);
        _rtmp_packet = nullptr;
        }, &_rtmp_packet->buffer);
}

RtmpPacket::Ptr H265RtmpEncoder::makeVideoConfigPkt() {
    RtmpPacket::Ptr rtmpPkt;
#ifdef ENABLE_MP4
    int8_t flags = FLV_CODEC_H265;
    flags |= (FLV_KEY_FRAME << 4);
    bool is_config = true;
    rtmpPkt = RtmpPacket::create();
    //header
    rtmpPkt->buffer.push_back(flags);
    rtmpPkt->buffer.push_back(!is_config);
    //cts
    rtmpPkt->buffer.append("\x0\x0\x0", 3);

    Track::Ptr track = std::make_shared<H265Track>(_sps, _pps, _vps, 0, 0, 0);
    auto extra_data = Factory::getDecodeInfo(track);
    //HEVCDecoderConfigurationRecord
    rtmpPkt->buffer.append((char *)extra_data.data(), extra_data.size());
    rtmpPkt->body_size = rtmpPkt->buffer.size();
    rtmpPkt->chunk_id = CHUNK_VIDEO;
    rtmpPkt->stream_index = STREAM_MEDIA;
    rtmpPkt->time_stamp = 0;
    rtmpPkt->type_id = MSG_VIDEO;
    RtmpCodec::inputRtmp(rtmpPkt);
#else
    WarnL << "请开启MP4相关功能并使能\"ENABLE_MP4\",否则对H265-RTMP支持不完善";
#endif
    return rtmpPkt;
}

}//namespace mediakit
