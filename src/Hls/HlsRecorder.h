/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef HLSRECORDER_H
#define HLSRECORDER_H

#include "TS/MPEG.h"
#include "Common/MediaSource.h"
#include "HlsMakerImp.h"
namespace mediakit {
class HlsRecorder final : public MediaSourceEventInterceptor, public MpegMuxer, public std::enable_shared_from_this<HlsRecorder> {
public:
    using Ptr = std::shared_ptr<HlsRecorder>;

    HlsRecorder(const std::string &m3u8_file, const std::string &params, const ProtocolOption &option);

    ~HlsRecorder();;

    void setMediaSource(const std::string &vhost, const std::string &app, const std::string &stream_id);

    void setListener(const std::weak_ptr<MediaSourceEvent> &listener);

    int readerCount();

    void onReaderChanged(MediaSource &sender, int size) override;

    bool inputFrame(const Frame::Ptr &frame) override;

    bool isEnabled();

    void makeSnap(int maxSec, const std::string& dir, SnapCB cb);
    float duration() const;

    static void setMaxCache(int maxSec);
private:
    void onWrite(std::shared_ptr<toolkit::Buffer> buffer, uint64_t timestamp, bool key_pos) override;

private:
    bool _enabled = true;
    bool _clear_cache = false;
    ProtocolOption _option;
    std::shared_ptr<HlsMakerImp> _hls;
};
}//namespace mediakit
#endif //HLSRECORDER_H
