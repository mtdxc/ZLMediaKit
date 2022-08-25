/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Decoder.h"
#include "PSDecoder.h"
#include "TSDecoder.h"
#include "Extension/H264.h"
#include "Extension/H265.h"
#include "Extension/AAC.h"
#include "Util/logger.h"
#if defined(ENABLE_RTPPROXY) || defined(ENABLE_HLS)
//#include "mpeg-ts-proto.h"
#include "ts_demux.hpp"
#endif

using namespace toolkit;

namespace mediakit {

void Decoder::setOnDecode(Decoder::onDecode cb) {
    _on_decode = std::move(cb);
}

void Decoder::setOnStream(Decoder::onStream cb) {
    _on_stream = std::move(cb);
}
    
static Decoder::Ptr createDecoder_l(DecoderImp::Type type) {
    switch (type){
        case DecoderImp::decoder_ps:
#ifdef ENABLE_RTPPROXY1
            return std::make_shared<PSDecoder>();
#else
            WarnL << "创建ps解复用器失败，请打开ENABLE_RTPPROXY然后重新编译";
            return nullptr;
#endif//ENABLE_RTPPROXY

        case DecoderImp::decoder_ts:
#ifdef ENABLE_HLS
            return std::make_shared<TSDecoder>();
#else
            WarnL << "创建mpegts解复用器失败，请打开ENABLE_HLS然后重新编译";
            return nullptr;
#endif//ENABLE_HLS

        default: return nullptr;
    }
}

/////////////////////////////////////////////////////////////

DecoderImp::Ptr DecoderImp::createDecoder(Type type, MediaSinkInterface *sink){
    auto decoder =  createDecoder_l(type);
    if(!decoder){
        return nullptr;
    }
    return DecoderImp::Ptr(new DecoderImp(decoder, sink));
}

void DecoderImp::flush() {
    _merger.flush();
}

ssize_t DecoderImp::input(const uint8_t *data, size_t bytes){
    return _decoder->input(data, bytes);
}

DecoderImp::DecoderImp(const Decoder::Ptr &decoder, MediaSinkInterface *sink){
    _decoder = decoder;
    _sink = sink;
    _decoder->setOnDecode([this](int stream, int codecid, int flags, int64_t pts, int64_t dts, const void *data, size_t bytes) {
        onDecode(stream, codecid, flags, pts, dts, data, bytes);
    });
    _decoder->setOnStream([this](int stream, int codecid, const void *extra, size_t bytes, int finish) {
        onStream(stream, codecid, extra, bytes, finish);
    });
}

#if defined(ENABLE_RTPPROXY) || defined(ENABLE_HLS)
// ISO/IEC 13818-1:2015 (E)
// 2.4.4.9 Semantic definition of fields in transport stream program map section
// Table 2-34 - Stream type assignments(p69)
enum EPSI_STREAM_TYPE
{
    PSI_STREAM_RESERVED = 0x00, // ITU-T | ISO/IEC Reserved
    PSI_STREAM_MPEG1 = 0x01, // ISO/IEC 11172-2 Video
    PSI_STREAM_MPEG2 = 0x02, // Rec. ITU-T H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2 constrained parameter video stream(see Note 2)
    PSI_STREAM_AUDIO_MPEG1 = 0x03, // ISO/IEC 11172-3 Audio
    PSI_STREAM_MP3 = 0x04, // ISO/IEC 13818-3 Audio
    PSI_STREAM_PRIVATE_SECTION = 0x05, // Rec. ITU-T H.222.0 | ISO/IEC 13818-1 private_sections
    PSI_STREAM_PRIVATE_DATA = 0x06, // Rec. ITU-T H.222.0 | ISO/IEC 13818-1 PES packets containing private data
    PSI_STREAM_MHEG = 0x07, // ISO/IEC 13522 MHEG
    PSI_STREAM_DSMCC = 0x08, // Rec. ITU-T H.222.0 | ISO/IEC 13818-1 Annex A DSM-CC
    PSI_STREAM_H222_ATM = 0x09, // Rec. ITU-T H.222.1
    PSI_STREAM_DSMCC_A = 0x0a, // ISO/IEC 13818-6(Extensions for DSM-CC) type A
    PSI_STREAM_DSMCC_B = 0x0b, // ISO/IEC 13818-6(Extensions for DSM-CC) type B
    PSI_STREAM_DSMCC_C = 0x0c, // ISO/IEC 13818-6(Extensions for DSM-CC) type C
    PSI_STREAM_DSMCC_D = 0x0d, // ISO/IEC 13818-6(Extensions for DSM-CC) type D
    PSI_STREAM_H222_Aux = 0x0e, // Rec. ITU-T H.222.0 | ISO/IEC 13818-1 auxiliary
    PSI_STREAM_AAC = 0x0f, // ISO/IEC 13818-7 Audio with ADTS transport syntax
    PSI_STREAM_MPEG4 = 0x10, // ISO/IEC 14496-2 Visual
    PSI_STREAM_MPEG4_AAC_LATM = 0x11, // ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3
    PSI_STREAM_MPEG4_PES = 0x12, // ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in PES packets
    PSI_STREAM_MPEG4_SECTIONS = 0x13, // ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in ISO/IEC 14496_sections
    PSI_STREAM_MPEG2_SDP = 0x14, // ISO/IEC 13818-6 Synchronized Download Protocol
    PSI_STREAM_PES_META = 0x15, // Metadata carried in PES packets
    PSI_STREAM_SECTION_META = 0x16, // Metadata carried in metadata_sections
    PSI_STREAM_DSMCC_DATA = 0x17, // Metadata carried in ISO/IEC 13818-6 Data Carousel
    PSI_STREAM_DSMCC_OBJECT = 0x18, // Metadata carried in ISO/IEC 13818-6 Object Carousel
    PSI_STREAM_DSMCC_SDP = 0x19, // Metadata carried in ISO/IEC 13818-6 Synchronized Download Protocol
    PSI_STREAM_MPEG2_IPMP = 0x1a, // IPMP stream (defined in ISO/IEC 13818-11, MPEG-2 IPMP)
    PSI_STREAM_H264 = 0x1b, // H.264
    PSI_STREAM_MPEG4_AAC = 0x1c, // ISO/IEC 14496-3 Audio, without using any additional transport syntax, such as DST, ALS and SLS
    PSI_STREAM_MPEG4_TEXT = 0x1d, // ISO/IEC 14496-17 Text
    PSI_STREAM_AUX_VIDEO = 0x1e, // Auxiliary video stream as defined in ISO/IEC 23002-3
    PSI_STREAM_H264_SVC = 0x1f, // SVC video sub-bitstream of an AVC video stream conforming to one or more profiles defined in Annex G of Rec. ITU-T H.264 | ISO/IEC 14496-10
    PSI_STREAM_H264_MVC = 0x20, // MVC video sub-bitstream of an AVC video stream conforming to one or more profiles defined in Annex H of Rec. ITU-T H.264 | ISO/IEC 14496-10
    PSI_STREAM_JPEG_2000 = 0x21, // Video stream conforming to one or more profiles as defined in Rec. ITU-T T.800 | ISO/IEC 15444-1
    PSI_STREAM_MPEG2_3D = 0x22, // Additional view Rec. ITU-T H.262 | ISO/IEC 13818-2 video stream for service-compatible stereoscopic 3D services
    PSI_STREAM_MPEG4_3D = 0x23, // Additional view Rec. ITU-T H.264 | ISO/IEC 14496-10 video stream conforming to one or more profiles defined in Annex A for service-compatible stereoscopic 3D services
    PSI_STREAM_H265 = 0x24, // Rec. ITU-T H.265 | ISO/IEC 23008-2 video stream or an HEVC temporal video sub-bitstream
    PSI_STREAM_H265_subset = 0x25, // HEVC temporal video subset of an HEVC video stream conforming to one or more profiles defined in Annex A of Rec. ITU-T H.265 | ISO/IEC 23008-2
    PSI_STREAM_H264_MVCD = 0x26, // MVCD video sub-bitstream of an AVC video stream conforming to one or more profiles defined in Annex I of Rec. ITU-T H.264 | ISO/IEC 14496-10
    PSI_STREAM_VP8 = 0x9d,
    PSI_STREAM_VP9 = 0x9e,
    PSI_STREAM_AV1 = 0x9f,
    // 0x27-0x7E Rec. ITU-T H.222.0 | ISO/IEC 13818-1 Reserved
    PSI_STREAM_IPMP = 0x7F, // IPMP stream
    // 0x80-0xFF User Private
    PSI_STREAM_VIDEO_CAVS = 0x42, // ffmpeg/libavformat/mpegts.h
    PSI_STREAM_AUDIO_AC3 = 0x81, // ffmpeg/libavformat/mpegts.h
    PSI_STREAM_AUDIO_EAC3 = 0x87, // ffmpeg/libavformat/mpegts.h
    PSI_STREAM_AUDIO_DTS = 0x8a, // ffmpeg/libavformat/mpegts.h
    PSI_STREAM_VIDEO_DIRAC = 0xd1, // ffmpeg/libavformat/mpegts.h
    PSI_STREAM_VIDEO_VC1 = 0xea, // ffmpeg/libavformat/mpegts.h
    PSI_STREAM_VIDEO_SVAC = 0x80, // GBT 25724-2010 SVAC(2014)
    PSI_STREAM_AUDIO_SVAC = 0x9B, // GBT 25724-2010 SVAC(2014)
    PSI_STREAM_AUDIO_G711A = 0x90,	// GBT 25724-2010 SVAC(2014)
    PSI_STREAM_AUDIO_G711U = 0x91,
    PSI_STREAM_AUDIO_G722 = 0x92,
    PSI_STREAM_AUDIO_G723 = 0x93,
    PSI_STREAM_AUDIO_G729 = 0x99,
    PSI_STREAM_AUDIO_OPUS = 0x9c,
};

#define SWITCH_CASE(codec_id) case codec_id : return #codec_id
static const char *getCodecName(int codec_id) {
    switch (codec_id) {
        SWITCH_CASE(PSI_STREAM_MPEG1);
        SWITCH_CASE(PSI_STREAM_MPEG2);
        SWITCH_CASE(PSI_STREAM_AUDIO_MPEG1);
        SWITCH_CASE(PSI_STREAM_MP3);
        SWITCH_CASE(PSI_STREAM_AAC);
        SWITCH_CASE(PSI_STREAM_MPEG4);
        SWITCH_CASE(PSI_STREAM_MPEG4_AAC_LATM);
        SWITCH_CASE(PSI_STREAM_H264);
        SWITCH_CASE(PSI_STREAM_MPEG4_AAC);
        SWITCH_CASE(PSI_STREAM_H265);
        SWITCH_CASE(PSI_STREAM_AUDIO_AC3);
        SWITCH_CASE(PSI_STREAM_AUDIO_EAC3);
        SWITCH_CASE(PSI_STREAM_AUDIO_DTS);
        SWITCH_CASE(PSI_STREAM_VIDEO_DIRAC);
        SWITCH_CASE(PSI_STREAM_VIDEO_VC1);
        SWITCH_CASE(PSI_STREAM_VIDEO_SVAC);
        SWITCH_CASE(PSI_STREAM_AUDIO_SVAC);
        SWITCH_CASE(PSI_STREAM_AUDIO_G711A);
        SWITCH_CASE(PSI_STREAM_AUDIO_G711U);
        SWITCH_CASE(PSI_STREAM_AUDIO_G722);
        SWITCH_CASE(PSI_STREAM_AUDIO_G723);
        SWITCH_CASE(PSI_STREAM_AUDIO_G729);
        SWITCH_CASE(PSI_STREAM_AUDIO_OPUS);
        default : return "unknown codec";
    }
}

void DecoderImp::onStream(int stream, int codecid, const void *extra, size_t bytes, int finish){
    switch (codecid) {
        case STREAM_TYPE_VIDEO_H264: {
            onTrack(std::make_shared<H264Track>());
            break;
        }

        case STREAM_TYPE_VIDEO_HEVC: {
            onTrack(std::make_shared<H265Track>());
            break;
        }

        case STREAM_TYPE_AUDIO_AAC: {
            onTrack(std::make_shared<AACTrack>());
            break;
        }
#if 0
        case PSI_STREAM_AUDIO_G711A:
        case PSI_STREAM_AUDIO_G711U: {
            auto codec = codecid == PSI_STREAM_AUDIO_G711A ? CodecG711A : CodecG711U;
            //G711传统只支持 8000/1/16的规格，FFmpeg貌似做了扩展，但是这里不管它了
            onTrack(std::make_shared<G711Track>(codec, 8000, 1, 16));
            break;
        }

        case PSI_STREAM_AUDIO_OPUS: {
            onTrack(std::make_shared<OpusTrack>());
            break;
        }
#endif
        default:
            if(codecid != 0){
                WarnL<< "unsupported codec type:" << getCodecName(codecid) << " " << (int)codecid;
            }
            break;
    }

    //防止未获取视频track提前complete导致忽略后续视频的问题，用于兼容一些不太规范的ps流
    if (finish && _tracks[TrackVideo] ) {
        _sink->addTrackCompleted();
        InfoL << "add track finished";
    }
}

void DecoderImp::onDecode(int stream, int codecid, int flags, int64_t pts, int64_t dts, const void* data, size_t bytes) {
    // 单位换算成ms
    pts /= 90;
    dts /= 90;

    switch (codecid) {
        case STREAM_TYPE_VIDEO_H264: {
            if (!_tracks[TrackVideo]) {
                onTrack(std::make_shared<H264Track>());
            }
            auto frame = std::make_shared<H264FrameNoCacheAble>((char *) data, bytes, (uint64_t)dts, (uint64_t)pts, prefixSize((char *) data, bytes));
            _merger.inputFrame(frame,[this](uint64_t dts, uint64_t pts, const Buffer::Ptr &buffer, bool) {
                onFrame(std::make_shared<FrameWrapper<H264FrameNoCacheAble> >(buffer, dts, pts, prefixSize(buffer->data(), buffer->size()), 0));
            });
            break;
        }

        case STREAM_TYPE_VIDEO_HEVC: {
            if (!_tracks[TrackVideo]) {
                onTrack(std::make_shared<H265Track>());
            }
            auto frame = std::make_shared<H265FrameNoCacheAble>((char *) data, bytes, (uint64_t)dts, (uint64_t)pts, prefixSize((char *) data, bytes));
            _merger.inputFrame(frame,[this](uint64_t dts, uint64_t pts, const Buffer::Ptr &buffer, bool) {
                onFrame(std::make_shared<FrameWrapper<H265FrameNoCacheAble> >(buffer, dts, pts, prefixSize(buffer->data(), buffer->size()), 0));
            });
            break;
        }

        case STREAM_TYPE_AUDIO_AAC: {
            uint8_t *ptr = (uint8_t *)data;
            if(!(bytes > ADTS_HEADER_LEN && ptr[0] == 0xFF && (ptr[1] & 0xF0) == 0xF0)){
                //这不是aac
                break;
            }
            if (!_tracks[TrackAudio]) {
                onTrack(std::make_shared<AACTrack>());
            }
            onFrame(std::make_shared<FrameFromPtr>(CodecAAC, (char *) data, bytes, (uint64_t)dts, 0, ADTS_HEADER_LEN));
            break;
        }
#if 0
        case PSI_STREAM_AUDIO_G711A:
        case PSI_STREAM_AUDIO_G711U: {
            auto codec = codecid  == PSI_STREAM_AUDIO_G711A ? CodecG711A : CodecG711U;
            if (!_tracks[TrackAudio]) {
                //G711传统只支持 8000/1/16的规格，FFmpeg貌似做了扩展，但是这里不管它了
                onTrack(std::make_shared<G711Track>(codec, 8000, 1, 16));
            }
            onFrame(std::make_shared<FrameFromPtr>(codec, (char *) data, bytes, (uint64_t)dts));
            break;
        }

        case PSI_STREAM_AUDIO_OPUS: {
            if (!_tracks[TrackAudio]) {
                onTrack(std::make_shared<OpusTrack>());
            }
            onFrame(std::make_shared<FrameFromPtr>(CodecOpus, (char *) data, bytes, (uint64_t)dts));
            break;
        }
#endif
        default:
            // 海康的 PS 流中会有 codecid 为 0xBD 的包
            if (codecid != 0 && codecid != 0xBD) {
                WarnL << "unsupported codec type:" << getCodecName(codecid) << " " << (int) codecid;
            }
            break;
    }
}
#else
void DecoderImp::onDecode(int stream,int codecid,int flags,int64_t pts,int64_t dts,const void *data,size_t bytes) {}
void DecoderImp::onStream(int stream,int codecid,const void *extra,size_t bytes,int finish) {}
#endif

void DecoderImp::onTrack(const Track::Ptr &track) {
    if (!_tracks[track->getTrackType()]) {
        _tracks[track->getTrackType()] = track;
        _sink->addTrack(track);
        InfoL << "got track: " << track->getCodecName();
    }
}

void DecoderImp::onFrame(const Frame::Ptr &frame) {
    _sink->inputFrame(frame);
}

}//namespace mediakit

