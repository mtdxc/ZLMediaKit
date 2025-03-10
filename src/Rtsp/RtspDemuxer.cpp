﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <cctype>
#include <algorithm>
#include "RtpCodec.h"
#include "RtspDemuxer.h"
#include "Util/base64.h"
#include "Extension/Factory.h"

using std::string;

namespace mediakit {

void RtspDemuxer::loadSdp(const string &sdp) {
    loadSdp(SdpParser(sdp));
}

void RtspDemuxer::loadSdp(const SdpParser &attr) {
    auto tracks = attr.getAvailableTrack();
    for (auto &track : tracks) {
        switch (track->_type) {
            case TrackVideo:
                makeVideoTrack(track);
                break;
            case TrackAudio:
                makeAudioTrack(track);
                break;
            default:
                break;
        }
    }
    //rtsp能通过sdp知道有几个track
    addTrackCompleted();

    auto titleTrack = attr.getTrack(TrackTitle);
    if (titleTrack) {
        _duration = titleTrack->_duration;
    }
}

float RtspDemuxer::getDuration() const {
    return _duration;
}

bool RtspDemuxer::inputRtp(const RtpPacket::Ptr &rtp) {
    switch (rtp->type) {
        case TrackVideo: {
            if (_video_rtp_decoder) {
                return _video_rtp_decoder->inputRtp(rtp, true);
            }
            return false;
        }
        case TrackAudio: {
            if (_audio_rtp_decoder) {
                _audio_rtp_decoder->inputRtp(rtp, false);
                return false;
            }
            return false;
        }
        default: 
            return false;
    }
}

static void setBitRate(const SdpTrack::Ptr &sdp, const Track::Ptr &track) {
    if (!sdp->_b.empty()) {
        track->setBitRate(sdp->getBitRate());
    }
}

void RtspDemuxer::makeAudioTrack(const SdpTrack::Ptr &audio) {
    if (_audio_rtp_decoder) {
        return;
    }
    //生成Track对象
    _audio_track = std::dynamic_pointer_cast<AudioTrack>(Factory::getTrackBySdp(audio));
    if (!_audio_track) {
        return;
    }
    setBitRate(audio, _audio_track);
    //生成RtpCodec对象以便解码rtp
    _audio_rtp_decoder = Factory::getRtpDecoderByTrack(_audio_track);
    if (!_audio_rtp_decoder) {
        //找不到相应的rtp解码器，该track无效
        _audio_track.reset();
        return;
    }
    //设置rtp解码器代理，生成的frame写入该Track
    _audio_rtp_decoder->addDelegate(_audio_track);
    addTrack(_audio_track);
}

void RtspDemuxer::makeVideoTrack(const SdpTrack::Ptr &video) {
    if (_video_rtp_decoder) {
        return;
    }
    //生成Track对象
    _video_track = std::dynamic_pointer_cast<VideoTrack>(Factory::getTrackBySdp(video));
    if (!_video_track) {
        return;
    }
    setBitRate(video, _video_track);
    //生成RtpCodec对象以便解码rtp
    _video_rtp_decoder = Factory::getRtpDecoderByTrack(_video_track);
    if (!_video_rtp_decoder) {
        //找不到相应的rtp解码器，该track无效
        _video_track.reset();
        return;
    }
    //设置rtp解码器代理，生成的frame写入该Track
    _video_rtp_decoder->addDelegate(_video_track);
    addTrack(_video_track);
}

} /* namespace mediakit */
