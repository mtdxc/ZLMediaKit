/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_TRANSCODE_H
#define ZLMEDIAKIT_TRANSCODE_H

#if defined(ENABLE_FFMPEG)
#include "Util/util.h"
#include "Util/TimeTicker.h"
#include "Common/MediaSink.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libswscale/swscale.h"
#include "libavutil/avutil.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/imgutils.h"
#ifdef __cplusplus
}
#endif

namespace mediakit {

class FFmpegFrame {
public:
    using Ptr = std::shared_ptr<FFmpegFrame>;

    FFmpegFrame(std::shared_ptr<AVFrame> frame = nullptr);
    ~FFmpegFrame();

    AVFrame *get() const;
    void fillPicture(AVPixelFormat target_format, int target_width, int target_height);

private:
    char *_data = nullptr;
    std::shared_ptr<AVFrame> _frame;
    toolkit::ObjectStatistic<FFmpegFrame> _counter;
};

class FFmpegSwr {
public:
    using Ptr = std::shared_ptr<FFmpegSwr>;

    FFmpegSwr(AVSampleFormat output, int channel, int channel_layout, int samplerate);
    ~FFmpegSwr();
    FFmpegFrame::Ptr inputFrame(const FFmpegFrame::Ptr &frame);

private:
    int _target_channels;
    int _target_channel_layout;
    int _target_samplerate;
    AVSampleFormat _target_format;
    SwrContext *_ctx = nullptr;
    toolkit::ObjectStatistic<FFmpegSwr> _statistic;
};

class FFmpegAudioFifo {
public:
    FFmpegAudioFifo() = default;
    ~FFmpegAudioFifo();

    bool Write(const AVFrame *frame);
    bool Read(AVFrame *frame, int sample_size);
    int size() const;

private:
    int _channels = 0;
    int _samplerate = 0;
    double _tsp = 0;
    double _timebase = 0;
    AVAudioFifo *_fifo = nullptr;
    AVSampleFormat _format = AV_SAMPLE_FMT_NONE;
};

class TaskManager {
public:
    virtual ~TaskManager();

    size_t getMaxTaskSize() const { return _max_task; }
    void setMaxTaskSize(size_t size);
    void stopThread(bool drop_task);

    void addDecodeDrop() {_decode_drop++;}
    int getDecodeDrop() const {return _decode_drop;}
protected:
    void startThread(const std::string &name);
    bool addEncodeTask(std::function<void()> task);
    bool addDecodeTask(bool key_frame, std::function<void()> task);
    bool isEnabled() const;

private:
    void onThreadRun(const std::string &name);

private:
    class ThreadExitException : public std::runtime_error {
    public:
        ThreadExitException() : std::runtime_error("exit") {}
    };

private:
    int _decode_drop = 0;
    bool _exit = false;
    size_t _max_task = 30;
    std::mutex _task_mtx;
    toolkit::semaphore _sem;
    toolkit::List<std::function<void()> > _task;
    std::shared_ptr<std::thread> _thread;
};

class FFmpegDecoder : public TaskManager, public CodecInfo {
public:
    using Ptr = std::shared_ptr<FFmpegDecoder>;
    using onDec = std::function<void(const FFmpegFrame::Ptr &)>;

    FFmpegDecoder(const Track::Ptr &track, int thread_num = 2, const std::vector<std::string> &codec_name = {});
    ~FFmpegDecoder() override;

    bool inputFrame(const Frame::Ptr &frame, bool live, bool async, bool enable_merge = true);
    void setOnDecode(onDec cb);
    void flush();
    CodecId getCodecId() const override { return _codecId; }
    const AVCodecContext *getContext() const { return _context.get(); }

private:
    void onDecode(const FFmpegFrame::Ptr &frame);
    bool inputFrame_l(const Frame::Ptr &frame, bool live, bool enable_merge);
    bool decodeFrame(const char *data, size_t size, uint64_t dts, uint64_t pts, bool live, bool key_frame);

private:
    CodecId _codecId;
    int _frames = 0;
    // default merge frame
    bool _do_merger = true;
    toolkit::Ticker _ticker;
    onDec _cb;
    std::shared_ptr<AVCodecContext> _context;
    FrameMerger _merger{FrameMerger::h264_prefix};
    toolkit::ObjectStatistic<FFmpegDecoder> _counter;
};

class FFmpegSws {
public:
    using Ptr = std::shared_ptr<FFmpegSws>;

    FFmpegSws(AVPixelFormat output, int width, int height);
    ~FFmpegSws();
    FFmpegFrame::Ptr inputFrame(const FFmpegFrame::Ptr &frame);
    int inputFrame(const FFmpegFrame::Ptr &frame, uint8_t *data);

private:
    FFmpegFrame::Ptr inputFrame(const FFmpegFrame::Ptr &frame, int &ret, uint8_t *data);

private:
    int _target_width = 0;
    int _target_height = 0;
    int _src_width = 0;
    int _src_height = 0;
    SwsContext *_ctx = nullptr;
    AVPixelFormat _src_format = AV_PIX_FMT_NONE;
    AVPixelFormat _target_format = AV_PIX_FMT_NONE;
    toolkit::ObjectStatistic<FFmpegSws> _counter;
};

class FFmpegEncoder : public TaskManager, public CodecInfo {
public:
    using Ptr = std::shared_ptr<FFmpegEncoder>;
    using onEnc = std::function<void(const Frame::Ptr &)>;

    FFmpegEncoder(const Track::Ptr &track, int thread_num = 2, int format = -1, const std::vector<std::string>& codec_name = {});
    ~FFmpegEncoder() override;

    void flush();
    CodecId getCodecId() const override { return _codecId; }
    const AVCodecContext *getContext() const { return _context.get(); }

    void setOnEncode(onEnc cb) { _cb = std::move(cb); }
    bool inputFrame(const FFmpegFrame::Ptr &frame, bool async);

private:
    void inputFrame_l(FFmpegFrame::Ptr frame);
    bool encodeFrame(AVFrame *frame);
    void onEncode(AVPacket *packet);

private:
    CodecId _codecId;
    int _frames = 0;
    onEnc _cb;

    std::shared_ptr<AVCodecContext> _context;
    std::unique_ptr<FFmpegSws> _sws;
    std::unique_ptr<FFmpegSwr> _swr;
    std::unique_ptr<FFmpegAudioFifo> _fifo;
    toolkit::ObjectStatistic<FFmpegEncoder> _counter;
};
}//namespace mediakit
#endif// ENABLE_FFMPEG
#endif //ZLMEDIAKIT_TRANSCODE_H
