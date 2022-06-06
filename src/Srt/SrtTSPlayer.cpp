/*
 * Copyright (c) 2020 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "SrtTSPlayer.h"
#include "Http\HlsPlayer.h"
#include "srt\udt.h"
using namespace toolkit;

namespace mediakit {

SrtTSPlayer::SrtTSPlayer(const EventPoller::Ptr &poller) : SrtClient(poller) {
}

void SrtTSPlayer::emitOnComplete(const SockException &ex) {
    if (_on_complete) {
        _on_complete(ex);
        _on_complete = nullptr;
    }
}

void SrtTSPlayer::setOnComplete(onComplete cb) {
    _on_complete = std::move(cb);
}

void SrtTSPlayer::play(const std::string &url)
{
    std::string stream_id;
    UDT::setstreamid(getSock()->rawFD(), stream_id);
}

void SrtTSPlayer::teardown()
{
    shutdown(SockException(Err_shutdown, "teardown"));
}

std::vector<Track::Ptr> SrtTSPlayer::getTracks(bool ready /*= true*/) const
{
    std::vector<Track::Ptr> ret;
    for (auto track : _tracks) {
        if (track)
            ret.push_back(track);
    }
    return ret;
}

bool SrtTSPlayer::inputFrame(const Frame::Ptr &frame)
{
    Track::Ptr track = _tracks[frame->getTrackType()];
    if (track) {
        track->inputFrame(frame);
    }
    return true;
}

void SrtTSPlayer::onConnect(const SockException &ex)
{
    emitOnComplete(ex);
    if (!ex) {
        if (!_decoder) {
            // 将ts_segment注入到 MediaSink: _demuxer
            _decoder = DecoderImp::createDecoder(DecoderImp::decoder_ts, this);
            if (!_decoder) return;
        }
    }
}

void SrtTSPlayer::onRecv(const Buffer::Ptr &buf)
{
    if (_decoder)
        _decoder->input((const uint8_t*)buf->data(), buf->size());
}

void SrtTSPlayer::onErr(const SockException &ex)
{
    emitOnComplete(ex);
}

} // namespace mediakit
