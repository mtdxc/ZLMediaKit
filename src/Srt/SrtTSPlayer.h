/*
 * Copyright (c) 2020 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef HTTP_SrtTSPlayer_H
#define HTTP_SrtTSPlayer_H

#include "Network/SrtClient.h"
#include "Player/MediaPlayer.h"
#include "Rtp/TSDecoder.h"

namespace mediakit {

/// http-ts播发器，未实现ts解复用
class SrtTSPlayer : public toolkit::SrtClient, public PlayerBase, private MediaSinkInterface {
public:
    using Ptr = std::shared_ptr<SrtTSPlayer>;
    using onComplete = std::function<void(const toolkit::SockException &)>;

    SrtTSPlayer(const toolkit::EventPoller::Ptr &poller = nullptr);
    ~SrtTSPlayer() = default;

    /**
     * 设置下载完毕或异常断开回调
     */
    void setOnComplete(onComplete cb);

    virtual void play(const std::string &url);
    virtual void teardown();

    virtual bool addTrack(const Track::Ptr & track) override {
        _tracks[track->getTrackType()] = track;
        return true;
    }
    virtual void addTrackCompleted() override {}
    virtual void resetTracks() { 
        for (auto& track : _tracks) {
            track = nullptr;
        }
    }
    std::vector<Track::Ptr> getTracks(bool ready = true) const override;

    virtual bool inputFrame(const Frame::Ptr &frame) override;

protected:
    virtual void onConnect(const toolkit::SockException &ex) override;
    virtual void onRecv(const toolkit::Buffer::Ptr &buf) override;
    virtual void onErr(const toolkit::SockException &ex) override;

private:
    void emitOnComplete(const toolkit::SockException &ex);

private:
    onComplete _on_complete;
    DecoderImp::Ptr _decoder;
    Track::Ptr _tracks[TrackMax];
};

}//namespace mediakit
#endif //HTTP_SrtTSPlayer_H
