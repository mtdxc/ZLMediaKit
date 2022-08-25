/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_MPEG_H
#define ZLMEDIAKIT_MPEG_H

#include "Common/MediaSink.h"

#if defined(ENABLE_HLS) || defined(ENABLE_RTPPROXY)

#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include "Ap4Mpeg2Ts.h"
class AP4_MemoryByteStream;
class AP4_SampleDescription;
namespace mediakit {

//该类用于产生MPEG-TS/MPEG-PS
class MpegMuxer : public MediaSinkInterface {
public:
    MpegMuxer(bool is_ps);
    ~MpegMuxer() override;

    /**
     * 添加音视频轨道
     */
    bool addTrack(const Track::Ptr &track) override;

    /**
     * 重置音视频轨道
     */
    void resetTracks() override;

    /**
     * 输入帧数据
     */
    bool inputFrame(const Frame::Ptr &frame) override;

    /**
     * 刷新输出所有frame缓存
     */
    void flush() override;

protected:
    /**
     * 输出ts/ps数据回调
     * @param buffer ts/ps数据包
     * @param timestamp 时间戳，单位毫秒
     * @param key_pos 是否为关键帧的第一个ts/ps包，用于确保ts切片第一帧为关键帧
     */
    virtual void onWrite(std::shared_ptr<toolkit::Buffer> buffer, uint64_t timestamp, bool key_pos) = 0;

private:
    void createContext();
    void releaseContext();
    void writeHeader();
private:
    bool _write_header = false;
    bool _is_ps = false;
    bool _have_video = false;
    bool _key_pos = false;
    uint64_t _timestamp = 0;
    AP4_Mpeg2TsWriter _writer;
    AP4_MemoryByteStream* _buffer;
    struct TsStream {
        AP4_Mpeg2TsWriter::SampleStream* stream;
        AP4_SampleDescription* desc;
    };
    std::unordered_map<int, TsStream> _streams;
    FrameMerger _frame_merger{FrameMerger::mp4_nal_size};
};

}//mediakit

#else

namespace mediakit {

class MpegMuxer : public MediaSinkInterface {
public:
    MpegMuxer(bool is_ps) {};
    ~MpegMuxer() override = default;
    bool addTrack(const Track::Ptr &track) override { return false; }
    void resetTracks() override {}
    bool inputFrame(const Frame::Ptr &frame) override { return false; }

protected:
    virtual void onWrite(std::shared_ptr<toolkit::Buffer> buffer, uint64_t timestamp, bool key_pos) = 0;
};

}//namespace mediakit

#endif

#endif //ZLMEDIAKIT_MPEG_H
