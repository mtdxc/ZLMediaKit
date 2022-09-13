/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Factory.h"
#include "Rtmp/Rtmp.h"
#include "H264Rtmp.h"
#include "H265Rtmp.h"
#include "AACRtmp.h"
#include "CommonRtmp.h"
#include "H264Rtp.h"
#include "AACRtp.h"
#include "H265Rtp.h"
#include "CommonRtp.h"
#include "G711Rtp.h"
#include "AudioTrack.h"
#include "Common/Parser.h"
#include "Common/config.h"
#include "base64.h"
#include "Ap4.h"
using namespace std;

namespace mediakit{
std::string Factory::getDecodeInfo(const Track::Ptr& track) {
    std::string ret;
    AP4_SampleDescription* desc = getAP4Descripion(track);
    if (!desc) return ret;
    switch (track->getCodecId())
    {
    case CodecH264:
        if (auto vDesc = AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, desc)) {
            ret.assign((const char*)vDesc->GetRawBytes().GetData(), vDesc->GetRawBytes().GetDataSize());
        }
        break;
    case CodecH265:
        if (auto vDesc = AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, desc)) {
            ret.assign((const char*)vDesc->GetRawBytes().GetData(), vDesc->GetRawBytes().GetDataSize());
        }
        break;
    case CodecAAC:
        if (auto audio = std::dynamic_pointer_cast<AACTrack>(track)) {
            ret = audio->getAacCfg();
        }
        /*
        if (auto aDesc = AP4_DYNAMIC_CAST(AP4_MpegAudioSampleDescription, desc)) {
            ret.assign((const char*)aDesc->GetRawBytes().GetData(), aDesc->GetRawBytes().GetDataSize());
        }*/
        break;
        break;
    default:
        break;
    }
    delete desc;
    return ret;
}

AP4_SampleDescription* Factory::getAP4Descripion(const Track::Ptr& track) {
    AP4_SampleDescription* ret = nullptr;
    AP4_UI32 format = 0;
    switch (track->getCodecId()) {
    case CodecH264:
        if (track->ready()) {
			format = AP4_SAMPLE_FORMAT_AVC1;
			auto video = std::dynamic_pointer_cast<H264Track>(track);
            AP4_AvcFrameParser::AccessUnitInfo access_unit_info;
            AP4_AvcFrameParser parser;
            parser.Feed((const AP4_UI08*)video->getSps().data(), video->getSps().size(), access_unit_info);
            parser.Feed((const AP4_UI08*)video->getPps().data(), video->getPps().size(), access_unit_info);
            unsigned int video_width = 0;
            unsigned int video_height = 0;
            AP4_AvcSequenceParameterSet* sps = parser.GetSequenceParameterSets()[0];
            sps->GetInfo(video_width, video_height);
            AP4_Array<AP4_DataBuffer> sps_array;
            sps_array.Append(AP4_DataBuffer((const AP4_UI08*)video->getSps().data(), video->getSps().size()));
            AP4_Array<AP4_DataBuffer> pps_array;
            sps_array.Append(AP4_DataBuffer((const AP4_UI08*)video->getPps().data(), video->getPps().size()));
            return new AP4_AvcSampleDescription(format,
                (AP4_UI16)video_width,
                (AP4_UI16)video_height,
                24,
                "h264",
                (AP4_UI08)sps->profile_idc,
                (AP4_UI08)sps->level_idc,
                (AP4_UI08)(sps->constraint_set0_flag << 7 |
                    sps->constraint_set1_flag << 6 |
                    sps->constraint_set2_flag << 5 |
                    sps->constraint_set3_flag << 4),
                4,
                sps->chroma_format_idc,
                sps->bit_depth_luma_minus8,
                sps->bit_depth_chroma_minus8,
                sps_array,
                pps_array);
        }
        break;
    case CodecH265:
        if (track->ready()) {
            auto video = std::dynamic_pointer_cast<H265Track>(track);
            format = AP4_SAMPLE_FORMAT_HEV1;
            AP4_HevcFrameParser::AccessUnitInfo access_unit_info;
            AP4_HevcFrameParser parser;
            parser.Feed((const AP4_UI08*)video->getSps().data(), video->getSps().size(), access_unit_info);
            parser.Feed((const AP4_UI08*)video->getPps().data(), video->getPps().size(), access_unit_info);
            parser.Feed((const AP4_UI08*)video->getVps().data(), video->getVps().size(), access_unit_info);
            AP4_HevcSequenceParameterSet* sps = parser.GetSequenceParameterSets()[0];
            unsigned int video_width = 0;
            unsigned int video_height = 0;
            sps->GetInfo(video_width, video_height);

            AP4_UI08 general_profile_space = sps->profile_tier_level.general_profile_space;
            AP4_UI08 general_tier_flag = sps->profile_tier_level.general_tier_flag;
            AP4_UI08 general_profile = sps->profile_tier_level.general_profile_idc;
            AP4_UI32 general_profile_compatibility_flags = sps->profile_tier_level.general_profile_compatibility_flags;
            AP4_UI64 general_constraint_indicator_flags = sps->profile_tier_level.general_constraint_indicator_flags;
            AP4_UI08 general_level = sps->profile_tier_level.general_level_idc;
            AP4_UI32 min_spatial_segmentation = 0; // TBD (should read from VUI if present)
            AP4_UI08 parallelism_type = 0; // unknown
            AP4_UI08 chroma_format = sps->chroma_format_idc;
            AP4_UI08 luma_bit_depth = 8; // hardcoded temporarily, should be read from the bitstream
            AP4_UI08 chroma_bit_depth = 8; // hardcoded temporarily, should be read from the bitstream
            AP4_UI16 average_frame_rate = 0; // unknown
            AP4_UI08 constant_frame_rate = 0; // unknown
            AP4_UI08 num_temporal_layers = 0; // unknown
            AP4_UI08 temporal_id_nested = 0; // unknown
            AP4_UI08 nalu_length_size = 4;

            // collect the VPS, SPS and PPS into arrays
            AP4_Array<AP4_DataBuffer> sps_array;
            sps_array.Append(AP4_DataBuffer((const AP4_UI08*)video->getSps().data(), video->getSps().size()));
            AP4_Array<AP4_DataBuffer> pps_array;
            sps_array.Append(AP4_DataBuffer((const AP4_UI08*)video->getPps().data(), video->getPps().size()));
            AP4_Array<AP4_DataBuffer> vps_array;
            sps_array.Append(AP4_DataBuffer((const AP4_UI08*)video->getVps().data(), video->getVps().size()));
            AP4_UI08 parameters_completeness = (format == AP4_SAMPLE_FORMAT_HVC1 ? 1 : 0);
            return new AP4_HevcSampleDescription(format,
                video_width,
                video_height,
                24,
                "HEVC Coding",
                general_profile_space,
                general_tier_flag,
                general_profile,
                general_profile_compatibility_flags,
                general_constraint_indicator_flags,
                general_level,
                min_spatial_segmentation,
                parallelism_type,
                chroma_format,
                luma_bit_depth,
                chroma_bit_depth,
                average_frame_rate,
                constant_frame_rate,
                num_temporal_layers,
                temporal_id_nested,
                nalu_length_size,
                vps_array,
                parameters_completeness,
                sps_array,
                parameters_completeness,
                pps_array,
                parameters_completeness);
        }
        break;
    case CodecAAC:
        if (track->ready()) {
            auto audio = std::dynamic_pointer_cast<AACTrack>(track);
            // create a sample description for our samples
            AP4_DataBuffer dsi;
            std::string cfg = audio->getAacCfg();
            dsi.SetData((const AP4_Byte*)cfg.data(), cfg.size());
            return new AP4_MpegAudioSampleDescription(
                AP4_OTI_MPEG4_AUDIO,   // object type
                audio->getAudioSampleRate(),
                16,                    // sample size
                audio->getAudioChannel(),
                &dsi,                  // decoder info
                6144,                  // buffer size
                128000,                // max bitrate
                128000);               // average bitrate
            //sample_description_index = sample_table->GetSampleDescriptionCount();
            //sample_table->AddSampleDescription(sample_description);
        }
        break;
    case CodecL16:
    case CodecOpus:
        format = AP4_SAMPLE_FORMAT_OPUS;
        break;
    case CodecG711A:
        format = AP4_SAMPLE_FORMAT_PCMA;
        break;
    case CodecG711U:
        format = AP4_SAMPLE_FORMAT_PCMU;
        break;
    default:
        break;
    }
    if (format) {
        auto audio = std::dynamic_pointer_cast<AudioTrack>(track);
        ret = new AP4_GenericAudioSampleDescription(format, audio->getAudioSampleRate(), audio->getAudioSampleBit(), audio->getAudioChannel(), nullptr);
    }
    else {
        WarnL << "暂不支持该CodecId:" << track->getCodecName();
    }
    return ret;
}

Track::Ptr Factory::getTrackBySdp(const SdpTrack::Ptr &track) {
    auto codec = getCodecId(track->_codec);
    if (codec == CodecInvalid) {
        //根据传统的payload type 获取编码类型以及采样率等信息
        codec = RtpPayload::getCodecId(track->_pt);
    }
    switch (codec) {
        case CodecG711A:
        case CodecG711U: 
            return std::make_shared<G711Track>(codec, track->_samplerate, track->_channel, 16);
        case CodecL16:  
            return std::make_shared<L16Track>(track->_samplerate, track->_channel);
        case CodecOpus : 
            return std::make_shared<OpusTrack>();
        case CodecAAC : {
            string aac_cfg_str = FindField(track->_fmtp.data(), "config=", ";");
            if (aac_cfg_str.empty()) {
                aac_cfg_str = FindField(track->_fmtp.data(), "config=", nullptr);
            }
            if (aac_cfg_str.empty()) {
                //如果sdp中获取不到aac config信息，在rtp也无法获取，则忽略该Track
                return nullptr;
            }
            string aac_cfg;
            for (size_t i = 0; i < aac_cfg_str.size() / 2; ++i) {
                unsigned int cfg;
                sscanf(aac_cfg_str.substr(i * 2, 2).data(), "%02X", &cfg);
                cfg &= 0x00FF;
                aac_cfg.push_back((char) cfg);
            }
            return std::make_shared<AACTrack>(aac_cfg);
        }

        case CodecH264 : {
            //a=fmtp:96 packetization-mode=1;profile-level-id=42C01F;sprop-parameter-sets=Z0LAH9oBQBboQAAAAwBAAAAPI8YMqA==,aM48gA==
            auto map = Parser::parseArgs(track->_fmtp, ";", "=");
            auto sps_pps = map["sprop-parameter-sets"];
            string base64_SPS = FindField(sps_pps.data(), NULL, ",");
            string base64_PPS = FindField(sps_pps.data(), ",", NULL);
            auto sps = hv::Base64Decode(base64_SPS.data());
            auto pps = hv::Base64Decode(base64_PPS.data());
            if (sps.empty() || pps.empty()) {
                //如果sdp里面没有sps/pps,那么可能在后续的rtp里面恢复出sps/pps
                return std::make_shared<H264Track>();
            }
            return std::make_shared<H264Track>(sps, pps, 0, 0);
        }

        case CodecH265: {
            //a=fmtp:96 sprop-sps=QgEBAWAAAAMAsAAAAwAAAwBdoAKAgC0WNrkky/AIAAADAAgAAAMBlQg=; sprop-pps=RAHA8vA8kAA=
            auto map = Parser::parseArgs(track->_fmtp, ";", "=");
            auto vps = hv::Base64Decode(map["sprop-vps"].data());
            auto sps = hv::Base64Decode(map["sprop-sps"].data());
            auto pps = hv::Base64Decode(map["sprop-pps"].data());
            if (sps.empty() || pps.empty()) {
                //如果sdp里面没有sps/pps,那么可能在后续的rtp里面恢复出sps/pps
                return std::make_shared<H265Track>();
            }
            return std::make_shared<H265Track>(vps, sps, pps, 0, 0, 0);
        }

        default: {
            //其他codec不支持
            WarnL << "暂不支持该rtsp编码类型:" << track->getName();
            return nullptr;
        }
    }
}


Track::Ptr Factory::getTrackByAbstractTrack(const Track::Ptr& track) {
    auto codec = track->getCodecId();
    switch (codec) {
        case CodecG711A:
        case CodecG711U: {
            auto audio_track = std::dynamic_pointer_cast<AudioTrackImp>(track);
            return std::make_shared<G711Track>(codec, audio_track->getAudioSampleRate(), audio_track->getAudioChannel(), 16);
        }
        case CodecL16: {
            auto audio_track = std::dynamic_pointer_cast<AudioTrackImp>(track);
            return std::make_shared<L16Track>(audio_track->getAudioSampleRate(), audio_track->getAudioChannel());
        }
        case CodecAAC: return std::make_shared<AACTrack>();
        case CodecOpus: return std::make_shared<OpusTrack>();
        case CodecH265: return std::make_shared<H265Track>();
        case CodecH264: return std::make_shared<H264Track>();

        default: {
            //其他codec不支持
            WarnL << "暂不支持该该编码类型创建Track:" << track->getCodecName();
            return nullptr;
        }
    }
}

RtpCodec::Ptr Factory::getRtpEncoderByCodecId(CodecId codec_id, uint32_t sample_rate, uint8_t pt, uint32_t ssrc) {
    GET_CONFIG(uint32_t, audio_mtu, Rtp::kAudioMtuSize);
    GET_CONFIG(uint32_t, video_mtu, Rtp::kVideoMtuSize);
    auto type = getTrackType(codec_id);
    auto mtu = type == TrackVideo ? video_mtu : audio_mtu;
    auto interleaved = type * 2;
    switch (codec_id) {
        case CodecH264: return std::make_shared<H264RtpEncoder>(ssrc, mtu, sample_rate, pt, interleaved);
        case CodecH265: return std::make_shared<H265RtpEncoder>(ssrc, mtu, sample_rate, pt, interleaved);
        case CodecAAC: return std::make_shared<AACRtpEncoder>(ssrc, mtu, sample_rate, pt, interleaved);
        case CodecL16:
        case CodecOpus: return std::make_shared<CommonRtpEncoder>(codec_id, ssrc, mtu, sample_rate, pt, interleaved);
        case CodecG711A:
        case CodecG711U: {
            if (pt == Rtsp::PT_PCMA || pt == Rtsp::PT_PCMU) {
                return std::make_shared<G711RtpEncoder>(codec_id, ssrc, mtu, sample_rate, pt, interleaved, 1);
            }
            return std::make_shared<CommonRtpEncoder>(codec_id, ssrc, mtu, sample_rate, pt, interleaved);
        }
        default: WarnL << "暂不支持该CodecId:" << codec_id; return nullptr;
    }
}

RtpCodec::Ptr Factory::getRtpEncoderBySdp(const Sdp::Ptr &sdp) {
    // ssrc不冲突即可,可以为任意的32位整形
    static atomic<uint32_t> s_ssrc(0);
    uint32_t ssrc = s_ssrc++;
    if (!ssrc) {
        // ssrc不能为0
        ssrc = 1;
    }
    if (sdp->getTrackType() == TrackVideo) {
        //视频的ssrc是偶数，方便调试
        ssrc = 2 * ssrc;
    } else {
        //音频ssrc是奇数
        ssrc = 2 * ssrc + 1;
    }
    return getRtpEncoderByCodecId(sdp->getCodecId(), sdp->getSampleRate(), sdp->getPayloadType(), ssrc);
}

RtpCodec::Ptr Factory::getRtpDecoderByTrack(const Track::Ptr &track) {
    switch (track->getCodecId()){
        case CodecH264 : return std::make_shared<H264RtpDecoder>();
        case CodecH265 : return std::make_shared<H265RtpDecoder>();
        case CodecAAC : return std::make_shared<AACRtpDecoder>(track->clone());
        case CodecL16 :
        case CodecOpus :
        case CodecG711A :
        case CodecG711U : return std::make_shared<CommonRtpDecoder>(track->getCodecId());
        default : WarnL << "暂不支持该CodecId:" << track->getCodecName(); return nullptr;
    }
}

/////////////////////////////rtmp相关///////////////////////////////////////////

static CodecId getVideoCodecIdByAmf(const AMFValue &val){
    if (val.type() == AMF_STRING) {
        auto str = val.as_string();
        if (str == "avc1") {
            return CodecH264;
        }
        if (str == "hev1" || str == "hvc1") {
            return CodecH265;
        }
        WarnL << "暂不支持该视频Amf:" << str;
    }
    else if (val.type() != AMF_NULL) {
        auto type_id = val.as_integer();
        switch (type_id) {
            case FLV_CODEC_H264 : return CodecH264;
            case FLV_CODEC_H265 : return CodecH265;
            default : WarnL << "暂不支持该视频Amf:" << type_id;;
        }
    }
    return CodecInvalid;
}

Track::Ptr getTrackByCodecId(CodecId codecId, int sample_rate = 0, int channels = 0, int sample_bit = 0) {
    switch (codecId){
        case CodecH264 : return std::make_shared<H264Track>();
        case CodecH265 : return std::make_shared<H265Track>();
        case CodecAAC : return std::make_shared<AACTrack>();
        case CodecOpus: return std::make_shared<OpusTrack>();
        case CodecG711A :
        case CodecG711U : 
            if(sample_rate && channels && sample_bit) 
                return std::make_shared<G711Track>(codecId, sample_rate, channels, sample_bit);
        default : 
            WarnL << "暂不支持该CodecId:" << codecId; 
            return nullptr;
    }
}

Track::Ptr Factory::getVideoTrackByAmf(const AMFValue &amf) {
    CodecId codecId = getVideoCodecIdByAmf(amf);
    if(codecId == CodecInvalid){
        return nullptr;
    }
    return getTrackByCodecId(codecId);
}

static CodecId getAudioCodecIdByAmf(const AMFValue &val) {
    if (val.type() == AMF_STRING) {
        auto str = val.as_string();
        if (str == "mp4a") {
            return CodecAAC;
        }
        WarnL << "暂不支持该音频Amf:" << str;
    }
    else if (val.type() != AMF_NULL) {
        auto type_id = val.as_integer();
        switch (type_id) {
            case FLV_CODEC_AAC : return CodecAAC;
            case FLV_CODEC_G711A : return CodecG711A;
            case FLV_CODEC_G711U : return CodecG711U;
            case FLV_CODEC_OPUS : return CodecOpus;
            default : WarnL << "暂不支持该音频Amf:" << type_id;
        }
    }
    return CodecInvalid;
}

Track::Ptr Factory::getAudioTrackByAmf(const AMFValue& amf, int sample_rate, int channels, int sample_bit){
    CodecId codecId = getAudioCodecIdByAmf(amf);
    if (codecId == CodecInvalid) {
        return nullptr;
    }
    return getTrackByCodecId(codecId, sample_rate, channels, sample_bit);
}

RtmpCodec::Ptr Factory::getRtmpCodecByTrack(const Track::Ptr &track, bool is_encode) {
    switch (track->getCodecId()){
        case CodecH264 :
            if (is_encode)
                return std::make_shared<H264RtmpEncoder>(track);
            else
                return std::make_shared<H264RtmpDecoder>();
        case CodecAAC :
            if(is_encode)
                return std::make_shared<AACRtmpEncoder>(track);
            else
                return std::make_shared<AACRtmpDecoder>();
        case CodecH265 :
            if(is_encode)
                return std::make_shared<H265RtmpEncoder>(track);
            else
                return std::make_shared<H265RtmpDecoder>();
        case CodecOpus :
            if (is_encode)
                return std::make_shared<CommonRtmpEncoder>(track);
            else
                return std::make_shared<CommonRtmpDecoder>(track->getCodecId());
        case CodecG711A :
        case CodecG711U : {
            auto audio_track = std::dynamic_pointer_cast<AudioTrack>(track);
            if (is_encode && (audio_track->getAudioSampleRate() != 8000 ||
                              audio_track->getAudioChannel() != 1 ||
                              audio_track->getAudioSampleBit() != 16)) {
                //rtmp对g711只支持8000/1/16规格，但是ZLMediaKit可以解析其他规格的G711
                WarnL << "RTMP只支持8000/1/16规格的G711,目前规格是:"
                      << audio_track->getAudioSampleRate() << "/"
                      << audio_track->getAudioChannel() << "/"
                      << audio_track->getAudioSampleBit()
                      << ",该音频已被忽略";
                return nullptr;
            }
            if(is_encode)
                return std::make_shared<CommonRtmpEncoder>(track);
            else
                return std::make_shared<CommonRtmpDecoder>(track->getCodecId());
        }
        default : 
            WarnL << "暂不支持该CodecId:" << track->getCodecName(); 
    }
    return nullptr;
}

AMFValue Factory::getAmfByCodecId(CodecId codecId) {
    switch (codecId){
        case CodecAAC: return AMFValue(FLV_CODEC_AAC);
        case CodecH264: return AMFValue(FLV_CODEC_H264);
        case CodecH265: return AMFValue(FLV_CODEC_H265);
        case CodecG711A: return AMFValue(FLV_CODEC_G711A);
        case CodecG711U: return AMFValue(FLV_CODEC_G711U);
        case CodecOpus: return AMFValue(FLV_CODEC_OPUS);
        default: return AMFValue(AMF_NULL);
    }
}

}//namespace mediakit

