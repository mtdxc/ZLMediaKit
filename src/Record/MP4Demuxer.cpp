/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifdef ENABLE_MP4
#include "MP4Demuxer.h"
#include "Util/logger.h"
#include "Rtmp/utils.h"
#include "Extension/H265.h"
#include "Extension/H264.h"
#include "Extension/AAC.h"
#include "Ap4.h"
using namespace toolkit;

namespace mediakit {
bool MP4Demuxer::Context::ReadSample()
{
    bool ret = false;
    if (!track || _eof) 
        return ret;
    
    if (!sample)
        sample = std::make_shared<AP4_Sample>();
    if (!data)
        data = std::make_shared<AP4_DataBuffer>();
    AP4_Result res = track->ReadSample(index++, *sample, *data);
    ret = res == AP4_SUCCESS;
    if (!ret) {
        _eof = true;
    }
    return ret;
}

bool MP4Demuxer::Context::Seek(int64_t stamp_ms)
{
    bool ret = false;
    if (!track) return ret;
    AP4_Result res = track->GetSampleIndexForTimeStampMs(stamp_ms, index);
    if (res == AP4_SUCCESS) {
        _eof = false;
        if (_track->getTrackType() == TrackVideo)
            index = track->GetNearestSyncSampleIndex(index);
        ReadSample();
        ret = true;
    }
    return ret;
}

MP4Demuxer::MP4Demuxer() {}

MP4Demuxer::~MP4Demuxer() {
    closeMP4();
}

void MP4Demuxer::openMP4(const std::string &file) {
    closeMP4();
    AP4_ByteStream* input_stream = NULL;
    AP4_Result result = AP4_FileByteStream::Create(file.c_str(),
        AP4_FileByteStream::STREAM_MODE_READ, input_stream);
    _file = new AP4_File(*input_stream, true);
    input_stream->Release();

    AP4_Movie *movie = _file->GetMovie();
    getAllTracks();
    _duration_ms = movie->GetDurationMs();
}

void MP4Demuxer::closeMP4() {
    if (_file) {
        delete _file;
        _file = nullptr;
    }
    _tracks.clear();
}

static AP4_Result MakeH264FramePrefix(AP4_SampleDescription* sdesc, AP4_DataBuffer& prefix, unsigned int& nalu_length_size)
{
    AP4_AvcSampleDescription* avc_desc = AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, sdesc);
    if (avc_desc == NULL) {
        fprintf(stderr, "ERROR: track does not contain an AVC stream\n");
        return AP4_FAILURE;
    }

    if (sdesc->GetFormat() == AP4_SAMPLE_FORMAT_AVC3 ||
        sdesc->GetFormat() == AP4_SAMPLE_FORMAT_AVC4 ||
        sdesc->GetFormat() == AP4_SAMPLE_FORMAT_DVAV) {
        // no need for a prefix, SPS/PPS NALs should be in the elementary stream already
        return AP4_SUCCESS;
    }

    // make the SPS/PPS prefix
    nalu_length_size = avc_desc->GetNaluLengthSize();
    for (unsigned int i = 0; i < avc_desc->GetSequenceParameters().ItemCount(); i++) {
        AP4_DataBuffer& buffer = avc_desc->GetSequenceParameters()[i];
        unsigned int prefix_size = prefix.GetDataSize();
        prefix.SetDataSize(prefix_size + 4 + buffer.GetDataSize());
        unsigned char* p = prefix.UseData() + prefix_size;
        *p++ = 0;
        *p++ = 0;
        *p++ = 0;
        *p++ = 1;
        AP4_CopyMemory(p, buffer.GetData(), buffer.GetDataSize());
    }
    for (unsigned int i = 0; i < avc_desc->GetPictureParameters().ItemCount(); i++) {
        AP4_DataBuffer& buffer = avc_desc->GetPictureParameters()[i];
        unsigned int prefix_size = prefix.GetDataSize();
        prefix.SetDataSize(prefix_size + 4 + buffer.GetDataSize());
        unsigned char* p = prefix.UseData() + prefix_size;
        *p++ = 0;
        *p++ = 0;
        *p++ = 0;
        *p++ = 1;
        AP4_CopyMemory(p, buffer.GetData(), buffer.GetDataSize());
    }

    return AP4_SUCCESS;
}

static AP4_Result MakeH265FramePrefix(AP4_SampleDescription* sdesc, AP4_DataBuffer& prefix, unsigned int& nalu_length_size)
{
    AP4_HevcSampleDescription* hevc_desc = AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, sdesc);
    if (hevc_desc == NULL) {
        fprintf(stderr, "ERROR: track does not contain an HEVC stream\n");
        return AP4_FAILURE;
    }

    // extract the nalu length size
    nalu_length_size = hevc_desc->GetNaluLengthSize();

    // make the VPS/SPS/PPS prefix
    for (unsigned int i = 0; i < hevc_desc->GetSequences().ItemCount(); i++) {
        const AP4_HvccAtom::Sequence& seq = hevc_desc->GetSequences()[i];
        if (seq.m_NaluType == AP4_HEVC_NALU_TYPE_VPS_NUT) {
            for (unsigned int j = 0; j < seq.m_Nalus.ItemCount(); j++) {
                const AP4_DataBuffer& buffer = seq.m_Nalus[j];
                unsigned int prefix_size = prefix.GetDataSize();
                prefix.SetDataSize(prefix_size + 4 + buffer.GetDataSize());
                unsigned char* p = prefix.UseData() + prefix_size;
                *p++ = 0;
                *p++ = 0;
                *p++ = 0;
                *p++ = 1;
                AP4_CopyMemory(p, buffer.GetData(), buffer.GetDataSize());
            }
        }
    }

    for (unsigned int i = 0; i < hevc_desc->GetSequences().ItemCount(); i++) {
        const AP4_HvccAtom::Sequence& seq = hevc_desc->GetSequences()[i];
        if (seq.m_NaluType == AP4_HEVC_NALU_TYPE_SPS_NUT) {
            for (unsigned int j = 0; j < seq.m_Nalus.ItemCount(); j++) {
                const AP4_DataBuffer& buffer = seq.m_Nalus[j];
                unsigned int prefix_size = prefix.GetDataSize();
                prefix.SetDataSize(prefix_size + 4 + buffer.GetDataSize());
                unsigned char* p = prefix.UseData() + prefix_size;
                *p++ = 0;
                *p++ = 0;
                *p++ = 0;
                *p++ = 1;
                AP4_CopyMemory(p, buffer.GetData(), buffer.GetDataSize());
            }
        }
    }

    for (unsigned int i = 0; i < hevc_desc->GetSequences().ItemCount(); i++) {
        const AP4_HvccAtom::Sequence& seq = hevc_desc->GetSequences()[i];
        if (seq.m_NaluType == AP4_HEVC_NALU_TYPE_PPS_NUT) {
            for (unsigned int j = 0; j < seq.m_Nalus.ItemCount(); j++) {
                const AP4_DataBuffer& buffer = seq.m_Nalus[j];
                unsigned int prefix_size = prefix.GetDataSize();
                prefix.SetDataSize(prefix_size + 4 + buffer.GetDataSize());
                unsigned char* p = prefix.UseData() + prefix_size;
                *p++ = 0;
                *p++ = 0;
                *p++ = 0;
                *p++ = 1;
                AP4_CopyMemory(p, buffer.GetData(), buffer.GetDataSize());
            }
        }
    }

    return AP4_SUCCESS;
}

int MP4Demuxer::getAllTracks() {
    AP4_Cardinal index = 0;
    AP4_Movie *movie = _file->GetMovie();
    AP4_List<AP4_Track>& tracks = movie->GetTracks();
    for (AP4_List<AP4_Track>::Item* track_item = tracks.FirstItem();
        index < tracks.ItemCount(); track_item = track_item->GetNext(), ++index) {
        AP4_Track *track = track_item->GetData();
        AP4_SampleDescription* sample_des = track->GetSampleDescription(0);
        switch (sample_des->GetFormat())
        {
        case AP4_SAMPLE_FORMAT_VP8:
        case AP4_SAMPLE_FORMAT_VP9:
            break;
        case AP4_SAMPLE_FORMAT_AVC1:
        case AP4_SAMPLE_FORMAT_AVC2:
        {
            auto video = std::make_shared<H264Track>();
            Context ctx;
            ctx.track = track;
            ctx._track = video;
            ctx.ReadSample();
            _tracks.push_back(ctx);
            unsigned int   nalu_length_size = 0;
            AP4_DataBuffer prefix;
            if (MakeH264FramePrefix(sample_des, prefix, nalu_length_size) == AP4_SUCCESS) {
                if (nalu_length_size > 0) {
                    auto buf = BufferRaw::create();
                    buf->assign((char*)prefix.GetData(), prefix.GetDataSize());
                    video->inputFrame(std::make_shared<FrameWrapper<H264FrameNoCacheAble>>(buf, 0, 0, 4, 0));
                }
            }
            break;
        }
        case AP4_SAMPLE_FORMAT_HVC1:
        case AP4_SAMPLE_FORMAT_HEV1:
        {
            auto video = std::make_shared<H264Track>();
            Context ctx;
            ctx.track = track;
            ctx._track = video;
            ctx.ReadSample();
            _tracks.push_back(ctx);

            unsigned int   nalu_length_size = 0;
            AP4_DataBuffer prefix;
            if (MakeH265FramePrefix(sample_des, prefix, nalu_length_size) == AP4_SUCCESS) {
                if (nalu_length_size > 0) {
                    auto buf = BufferRaw::create();
                    buf->assign((char*)prefix.GetData(), prefix.GetDataSize());
                    video->inputFrame(std::make_shared<FrameWrapper<H265FrameNoCacheAble>>(buf, 0, 0, 4, 0));
                }
            }
            break;
        }
        case AP4_SAMPLE_FORMAT_MP4A:
        {
            AP4_MpegAudioSampleDescription* audio_desc =
                AP4_DYNAMIC_CAST(AP4_MpegAudioSampleDescription, sample_des);
            if (audio_desc) {
                auto decinfo = audio_desc->GetDecoderInfo();
                AP4_Debug("decinfo %d\n", decinfo.GetDataSize());
                auto audio = std::make_shared<AACTrack>(std::string((char*)decinfo.GetData(), decinfo.GetDataSize()));
                Context ctx;
                ctx.track = track;
                ctx._track = audio;
                ctx.ReadSample();
                _tracks.push_back(ctx);
            }
            break;
        }
        case AP4_SAMPLE_FORMAT_PCMU:
        case AP4_SAMPLE_FORMAT_PCMA:
        {
            AP4_AudioSampleDescription* audio_desc =
                AP4_DYNAMIC_CAST(AP4_AudioSampleDescription, sample_des);
            auto audio = std::make_shared<G711Track>(sample_des->GetFormat() == AP4_SAMPLE_FORMAT_PCMA ? CodecG711A : CodecG711U, 
                audio_desc->GetSampleRate(), audio_desc->GetChannelCount(), 64000);
            Context ctx;
            ctx.track = track;
            ctx._track = audio;
            ctx.ReadSample();
            _tracks.push_back(ctx);
            break;
        }
        case AP4_SAMPLE_FORMAT_OPUS: 
        {
            auto audio = std::make_shared<OpusTrack>();
            Context ctx;
            ctx.track = track;
            ctx._track = audio;
            ctx.ReadSample();
            _tracks.push_back(ctx);
            break;
        }
        default:
            WarnL << "不支持该编码类型的MP4,已忽略:";// << getObjectName(object);
            break;
        }
    }
    return 0;
}

int64_t MP4Demuxer::seekTo(int64_t stamp_ms) {
    for (auto& track : _tracks)
    {
        if (!track.Seek(stamp_ms))
            return -1;
    }
    return stamp_ms;
}

#define DATA_OFFSET ADTS_HEADER_LEN

Frame::Ptr MP4Demuxer::readFrame(bool &keyFrame, bool &eof) {
    keyFrame = false;
    eof = false;
    int idx = 0;
    AP4_UI64 ctx = INTMAX_MAX;
    for (int i = 0; i < _tracks.size(); i++) {
        if (!_tracks[i]._eof && _tracks[i].sample->GetCts() < ctx) {
            idx = i;
            ctx = _tracks[i].sample->GetCts();
        }
    }
    auto& track = _tracks[idx];
    auto buf = toolkit::BufferRaw::create();
    buf->assign((char*)track.data->GetData(), track.data->GetDataSize());
    auto ret = makeFrame(idx, buf, track.sample->GetCts(), track.sample->GetDts());
    track.ReadSample();
    return ret;
}

Frame::Ptr MP4Demuxer::makeFrame(uint32_t track_id, const toolkit::Buffer::Ptr &buf, int64_t pts, int64_t dts) {
    if (track_id > _tracks.size()) {
        return nullptr;
    }

    auto bytes = buf->size() - DATA_OFFSET;
    auto data = buf->data() + DATA_OFFSET;
    auto track = _tracks[track_id]._track;
    auto codec = track->getCodecId();
    Frame::Ptr ret;
    switch (codec) {
        case CodecH264 :
        case CodecH265 : {
            uint32_t offset = 0;
            while (offset < bytes) {
                uint32_t frame_len = load_be32(data + offset);
                if (frame_len + offset + 4 > bytes) {
                    return nullptr;
                }
                memcpy(data + offset, "\x00\x00\x00\x01", 4);
                offset += (frame_len + 4);
            }
            if (codec == CodecH264)
                ret = std::make_shared<FrameWrapper<H264FrameNoCacheAble>>(buf, (uint32_t)dts, (uint32_t)pts, 4, DATA_OFFSET);
            else
                ret = std::make_shared<FrameWrapper<H265FrameNoCacheAble>>(buf, (uint32_t)dts, (uint32_t)pts, 4, DATA_OFFSET);
            break;
        }

        case CodecAAC: {
            AACTrack::Ptr track = std::dynamic_pointer_cast<AACTrack>(track);
            assert(track);
            //加上adts头
            dumpAacConfig(track->getAacCfg(), bytes, (uint8_t *) buf->data() + (DATA_OFFSET - ADTS_HEADER_LEN), ADTS_HEADER_LEN);
            ret = std::make_shared<FrameWrapper<FrameFromPtr>>(buf, (uint32_t)dts, (uint32_t)pts, ADTS_HEADER_LEN, DATA_OFFSET - ADTS_HEADER_LEN, codec);
            break;
        }

        case CodecOpus:
        case CodecG711A:
        case CodecG711U:
            ret = std::make_shared<FrameWrapper<FrameFromPtr> >(buf, (uint32_t)dts, (uint32_t)pts, 0, DATA_OFFSET, codec);
            break;
        default: 
            return nullptr;
    }
    if (ret) {
        track->inputFrame(ret);
    }
    return ret;
}

std::vector<Track::Ptr> MP4Demuxer::getTracks(bool trackReady) const {
    std::vector<Track::Ptr> ret;
    for (auto &pr : _tracks) {
        if(trackReady && !pr._track->ready()){
            continue;
        }
        ret.push_back(pr._track);
    }
    return ret;
}

uint64_t MP4Demuxer::getDurationMS() const {
    return _duration_ms;
}

}//namespace mediakit
#endif// ENABLE_MP4