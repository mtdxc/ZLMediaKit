/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#if defined(ENABLE_FFMPEG)
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
#include "Util/File.h"
#include "Util/util.h"
#include "Util/uv_errno.h"
#include <float.h>
#include "Transcode.h"
#include "Common/config.h"
#include "Extension/Factory.h"

#define MAX_DELAY_SECOND 3

using namespace std;
using namespace toolkit;

namespace mediakit {
// 配置项目
#define TRANSCODE_FIELD "transcode."
const string kH264DecoderList = TRANSCODE_FIELD "decoder_h264";
const string kH265DecoderList = TRANSCODE_FIELD "decoder_h265";
const string kH264EncoderList = TRANSCODE_FIELD "encoder_h264";
const string kH265EncoderList = TRANSCODE_FIELD "encoder_h265";

static onceToken token([]() {
    mINI::Instance()[kH264DecoderList] = "h264_cuvid,h264_qsv,h264_videotoolbox,h264_nvmpi,h264_bm,libopenh264";
    mINI::Instance()[kH265DecoderList] = "hevc_cuvid,hevc_qsv,hevc_videotoolbox,hevc_nvmpi,hevc_bm";
    mINI::Instance()[kH264EncoderList] = "h264_nvenc,h264_qsv,h264_videotoolbox,h264_nvmpi,h264_bm,libx264,libopenh264";
    mINI::Instance()[kH265EncoderList] = "hevc_nvenc,hevc_qsv,hevc_videotoolbox,hevc_nvmpi,hevc_bm,libx265";
});
static std::vector<std::string> ToStrList(const std::string &str) {
    return toolkit::split(str, ",");
}

static string ffmpeg_err(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return errbuf;
}

std::shared_ptr<AVPacket> alloc_av_packet() {
    auto pkt = std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket *pkt) {
        av_packet_free(&pkt);
    });
    pkt->data = NULL;    // packet data will be allocated by the encoder
    pkt->size = 0;
    return pkt;
}

//////////////////////////////////////////////////////////////////////////////////////////
static void on_ffmpeg_log(void *ctx, int level, const char *fmt, va_list args) {
    GET_CONFIG(bool, enable_ffmpeg_log, General::kEnableFFmpegLog);
    if (!enable_ffmpeg_log) {
        return;
    }
    LogLevel lev;
    switch (level) {
        case AV_LOG_FATAL: lev = LError; break;
        case AV_LOG_ERROR: lev = LError; break;
        case AV_LOG_WARNING: lev = LWarn; break;
        case AV_LOG_INFO: lev = LInfo; break;
        case AV_LOG_VERBOSE: lev = LDebug; break;
        case AV_LOG_DEBUG: lev = LDebug; break;
        case AV_LOG_TRACE: lev = LTrace; break;
        default: lev = LTrace; break;
    }
    LoggerWrapper::printLogV(::toolkit::getLogger(), lev, __FILE__, ctx ? av_default_item_name(ctx) : "NULL", level, fmt, args);
}

static bool setupFFmpeg_l() {
    av_log_set_level(AV_LOG_TRACE);
    av_log_set_flags(AV_LOG_PRINT_LEVEL);
    av_log_set_callback(on_ffmpeg_log);
#if (LIBAVCODEC_VERSION_MAJOR < 58)
    avcodec_register_all();
#endif
    InfoL << "libavcodec " << LIBAVCODEC_VERSION_MAJOR << "." << LIBAVCODEC_VERSION_MINOR;
    //InfoL << "libavformat " << LIBAVFORMAT_VERSION_MAJOR << "." << LIBAVFORMAT_VERSION_MINOR;
    InfoL << "libavutil " << LIBAVUTIL_VERSION_MAJOR << "." << LIBAVUTIL_VERSION_MAJOR;
    return true;
}

static void setupFFmpeg() {
    static auto flag = setupFFmpeg_l();
}

int selectVideoFormat(const AVCodec *codec, int format) {
    std::set<int> support_fmts;
    auto fmt = codec->pix_fmts;
    while (fmt && *fmt != AV_PIX_FMT_NONE) {
        //const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*fmt);
        //if (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
        support_fmts.emplace(*fmt);
        ++fmt;
    }
    if (support_fmts.empty()) {
        WarnL << "AVCodec pix_fmts is empty:" << codec->name;
        support_fmts.emplace(AV_PIX_FMT_YUV420P);
    }
    if (support_fmts.find(format) != support_fmts.end()) {
        //最佳format
        return format;
    } else if (support_fmts.find(AV_PIX_FMT_YUV420P) != support_fmts.end()) {
        return AV_PIX_FMT_YUV420P;
    } else {
        return *support_fmts.begin();
    }
}

int selectAudioFormat(const AVCodec *codec, int format) {
    std::set<int> support_fmts;
    auto fmt = codec->sample_fmts;
    while (fmt && *fmt != AV_PIX_FMT_NONE) {
        support_fmts.emplace(*fmt);
        ++fmt;
    }
    if (support_fmts.empty()) {
        WarnL << "AVCodec sample_fmts is empty:" << codec->name;
        support_fmts.emplace(AV_SAMPLE_FMT_S16);
    }
    if (support_fmts.find(format) != support_fmts.end()) {
        //最佳format
        return format;
    } else {
        return *support_fmts.begin();
    }
}

int selectAudioSamplerate(const AVCodec *codec, int rate) {
    set<int> supported_sample_rates;
    auto fmt = codec->supported_samplerates;
    while (fmt && *fmt != 0) {
        supported_sample_rates.emplace(*fmt);
        ++fmt;
    }
    if (supported_sample_rates.empty()) {
        WarnL << "AVCodec supported_samplerates is empty:" << codec->name;
        supported_sample_rates.emplace(8000);
    }
    if (supported_sample_rates.find(rate) == supported_sample_rates.end()) {
        rate = *supported_sample_rates.begin();
    }
    return rate;
}
//////////////////////////////////////////////////////////////////////////////////////////

bool TaskManager::addEncodeTask(function<void()> task) {
    {
        lock_guard<mutex> lck(_task_mtx);
        _task.emplace_back(std::move(task));
        if (_task.size() > _max_task) {
            WarnL << "encoder thread task is too more, now drop frame!";
            _task.pop_front();
        }
    }
    _sem.post();
    return true;
}

bool TaskManager::addDecodeTask(bool key_frame, function<void()> task) {
    {
        lock_guard<mutex> lck(_task_mtx);
        if (_decode_drop) {
            if (!key_frame) {
                TraceL << "decode thread drop frame " << _decode_drop;
                _decode_drop++;
                return false;
            }
            InfoL << "decode thread stop, after drop " << _decode_drop - 1 << " frames";
            _decode_drop = 0;
        }

        _task.emplace_back(std::move(task));
        if (_task.size() > _max_task) {
            _decode_drop = 1;
            WarnL << "decode thread start drop frame";
        }
    }
    _sem.post();
    return true;
}

void TaskManager::setMaxTaskSize(size_t size) {
    CHECK(size >= 3 && size <= 1000, "async task size limited to 3 ~ 1000, now size is:", size);
    _max_task = size;
}

void TaskManager::startThread(const string &name) {
    _thread.reset(new thread([this, name]() {
        onThreadRun(name);
    }), [](thread *ptr) {
        ptr->join();
        delete ptr;
    });
}

void TaskManager::stopThread(bool drop_task) {
    TimeTicker();
    if (!_thread) {
        return;
    }
    {
        lock_guard<mutex> lck(_task_mtx);
        if (drop_task) {
            _exit = true;
            _task.clear();
        }
        _task.emplace_back([]() {
            throw ThreadExitException();
        });
    }
    _sem.post(10);
    _thread = nullptr;
}

TaskManager::~TaskManager() {
    stopThread(true);
}

bool TaskManager::isEnabled() const {
    return _thread.operator bool();
}

void TaskManager::onThreadRun(const string &name) {
    setThreadName(name.data());
    function<void()> task;
    _exit = false;
    while (!_exit) {
        _sem.wait();
        {
            unique_lock<mutex> lck(_task_mtx);
            if (_task.empty()) {
                continue;
            }
            task = _task.front();
            _task.pop_front();
        }

        try {
            TimeTicker2(50, TraceL);
            task();
            task = nullptr;
        } catch (ThreadExitException &ex) {
            break;
        } catch (std::exception &ex) {
            WarnL << ex.what();
            continue;
        } catch (...) {
            WarnL << "catch one unknown exception";
            throw;
        }
    }
    InfoL << name << " exited!";
}

//////////////////////////////////////////////////////////////////////////////////////////

FFmpegFrame::FFmpegFrame(std::shared_ptr<AVFrame> frame) {
    if (frame) {
        _frame = std::move(frame);
    } else {
        _frame.reset(av_frame_alloc(), [](AVFrame *ptr) {
            av_frame_free(&ptr);
        });
    }
}

FFmpegFrame::~FFmpegFrame() {
    if (_data) {
        delete[] _data;
        _data = nullptr;
    }
}

AVFrame *FFmpegFrame::get() const {
    return _frame.get();
}

void FFmpegFrame::fillPicture(AVPixelFormat target_format, int target_width, int target_height) {
    assert(_data == nullptr);
    _data = new char[av_image_get_buffer_size(target_format, target_width, target_height, 32)];
    av_image_fill_arrays(_frame->data, _frame->linesize, (uint8_t *) _data,  target_format, target_width, target_height, 32);
}

///////////////////////////////////////////////////////////////////////////

template<bool decoder = true>
static inline const AVCodec *getCodec_l(const char *name) {
    auto codec = decoder ? avcodec_find_decoder_by_name(name) : avcodec_find_encoder_by_name(name);
    if (codec) {
        InfoL << (decoder ? "got decoder:" : "got encoder:") << name;
    } else {
        TraceL << (decoder ? "decoder:" : "encoder:") << name << " not found";
    }
    return codec;
}

template<bool decoder = true>
static inline const AVCodec *getCodec_l(enum AVCodecID id) {
    auto codec = decoder ? avcodec_find_decoder(id) : avcodec_find_encoder(id);
    if (codec) {
        InfoL << (decoder ? "got decoder:" : "got encoder:") << avcodec_get_name(id);
    } else {
        TraceL << (decoder ? "decoder:" : "encoder:") << avcodec_get_name(id) << " not found";
    }
    return codec;
}

class CodecName {
public:
    CodecName(string name) : _codec_name(std::move(name)) {}
    CodecName(enum AVCodecID id) : _id(id) {}

    template <bool decoder>
    const AVCodec *getCodec() const {
        if (!_codec_name.empty()) {
            return getCodec_l<decoder>(_codec_name.data());
        }
        return getCodec_l<decoder>(_id);
    }

private:
    string _codec_name;
    enum AVCodecID _id;
};

template <bool decoder = true>
static inline const AVCodec *getCodec(const std::initializer_list<CodecName> &codec_list) {
    const AVCodec *ret = nullptr;
    for (int i = codec_list.size(); i >= 1; --i) {
        ret = codec_list.begin()[i - 1].getCodec<decoder>();
        if (ret) {
            return ret;
        }
    }
    return ret;
}

template<bool decoder = true>
static inline const AVCodec *getCodecByName(const std::vector<std::string> &codec_list) {
    const AVCodec *ret = nullptr;
    for (auto &codec : codec_list) {
        ret = getCodec_l<decoder>(codec.data());
        if (ret) {
            return ret;
        }
    }
    return ret;
}

FFmpegDecoder::FFmpegDecoder(const Track::Ptr &track, int thread_num, const std::vector<std::string> &codec_name) {
    setupFFmpeg();
    _codecId = track->getCodecId();
    const AVCodec *codec = nullptr;
    const AVCodec *codec_default = nullptr;
    switch (track->getCodecId()) {
        case CodecH264:
            codec_default = getCodec({AV_CODEC_ID_H264});
            if (codec_name.size()) {
                codec = getCodecByName(codec_name);
            } else {
                GET_CONFIG_FUNC(std::vector<std::string>, h264DecList, kH264DecoderList, ToStrList);
                codec = getCodecByName(h264DecList);
            }
            break;
        case CodecH265:
            codec_default = getCodec({AV_CODEC_ID_HEVC});
            if (codec_name.size()) {
                codec = getCodecByName(codec_name);
            } else {
                GET_CONFIG_FUNC(std::vector<std::string>, h265DecList, kH265DecoderList, ToStrList);
                codec = getCodecByName(h265DecList);
            }
            break;
        case CodecAAC:
            if (codec && codec->id == AV_CODEC_ID_AAC) {
                break;
            }
            codec = getCodec({AV_CODEC_ID_AAC});
            break;
        case CodecG711A:
            if (codec && codec->id == AV_CODEC_ID_PCM_ALAW) {
                break;
            }
            codec = getCodec({AV_CODEC_ID_PCM_ALAW});
            break;
        case CodecG711U:
            if (codec && codec->id == AV_CODEC_ID_PCM_MULAW) {
                break;
            }
            codec = getCodec({AV_CODEC_ID_PCM_MULAW});
            break;
        case CodecOpus:
            if (codec && codec->id == AV_CODEC_ID_OPUS) {
                break;
            }
            codec = getCodec({AV_CODEC_ID_OPUS});
            break;
        case CodecJPEG:
            if (codec && codec->id == AV_CODEC_ID_MJPEG) {
                break;
            }
            codec = getCodec({AV_CODEC_ID_MJPEG});
            break;
        case CodecVP8:
            if (codec && codec->id == AV_CODEC_ID_VP8) {
                break;
            }
            codec = getCodec({AV_CODEC_ID_VP8});
            break;
        case CodecVP9:
            if (codec && codec->id == AV_CODEC_ID_VP9) {
                break;
            }
            codec = getCodec({AV_CODEC_ID_VP9});
            break;
        default: codec = nullptr; break;
    }

    codec = codec ? codec : codec_default;
    if (!codec) {
        throw std::runtime_error("未找到解码器");
    }

    while (true) {
        _context.reset(avcodec_alloc_context3(codec), [](AVCodecContext *ctx) {
            avcodec_free_context(&ctx);
        });

        if (!_context) {
            throw std::runtime_error("创建解码器失败");
        }

        // 保存AVFrame的引用  [AUTO-TRANSLATED:2df53d07]
        // Save the AVFrame reference
#ifdef FF_API_OLD_ENCDEC
        _context->refcounted_frames = 1;
#endif
        _context->flags |= AV_CODEC_FLAG_LOW_DELAY;
        _context->flags2 |= AV_CODEC_FLAG2_FAST;
        if (track->getTrackType() == TrackVideo) {
            auto video = static_pointer_cast<VideoTrack>(track);
            _context->width = video->getVideoWidth();
            _context->height = video->getVideoHeight();
            InfoL << "decode video " << video->getCodecName() << " " << _context->width << "x" << _context->height;
        } else {
            auto audio = static_pointer_cast<AudioTrack>(track);
            InfoL << "decode audio " << audio->getCodecName() << " " << audio->getAudioSampleRate() << "x" << audio->getAudioChannel();
            switch (track->getCodecId()) {
                case CodecG711A:
                case CodecG711U: {
#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
                    av_channel_layout_default(&_context->ch_layout, audio->getAudioChannel());
#else
                    _context->channels = audio->getAudioChannel();
                    _context->channel_layout = av_get_default_channel_layout(_context->channels);
#endif
                    _context->sample_rate = audio->getAudioSampleRate();
                    break;
                }
                default: break;
            }
        }
        AVDictionary *dict = nullptr;
        if (thread_num <= 0) {
            av_dict_set(&dict, "threads", "auto", 0);
        } else {
            av_dict_set(&dict, "threads", to_string(MIN((unsigned int)thread_num, thread::hardware_concurrency())).data(), 0);
        }
        av_dict_set(&dict, "zerolatency", "1", 0);
        av_dict_set(&dict, "strict", "-2", 0);

#ifdef AV_CODEC_CAP_TRUNCATED
        if (codec->capabilities & AV_CODEC_CAP_TRUNCATED) {
            /* we do not send complete frames */
            _context->flags |= AV_CODEC_FLAG_TRUNCATED;
            _do_merger = false;
        } else {
            // 此时业务层应该需要合帧  [AUTO-TRANSLATED:8dea0fff]
            // The business layer should need to merge frames at this time
            _do_merger = true;
        }
#endif

        int ret = avcodec_open2(_context.get(), codec, &dict);
        av_dict_free(&dict);
        if (ret >= 0) {
            // 成功  [AUTO-TRANSLATED:7d878ca9]
            // Success
            InfoL << "打开解码器成功:" << codec->name;
            break;
        }

        if (codec_default && codec_default != codec) {
            // 硬件编解码器打开失败，尝试软件的  [AUTO-TRANSLATED:060200f4]
            // Hardware codec failed to open, try software codec
            WarnL << "打开解码器" << codec->name << "失败，原因是:" << ffmpeg_err(ret) << ", 再尝试打开解码器" << codec_default->name;
            codec = codec_default;
            continue;
        }
        throw std::runtime_error(StrPrinter << "打开解码器" << codec->name << "失败:" << ffmpeg_err(ret));
    }
}

FFmpegDecoder::~FFmpegDecoder() {
    InfoL << getCodecName();
    stopThread(true);
    if (_do_merger) {
        _merger.flush();
    }
    flush();
}

void FFmpegDecoder::flush() {
    while (true) {
        auto out_frame = std::make_shared<FFmpegFrame>();
        auto ret = avcodec_receive_frame(_context.get(), out_frame->get());
        if (ret == AVERROR(EAGAIN)) {
            avcodec_send_packet(_context.get(), nullptr);
            continue;
        }
        if (ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            WarnL << "avcodec_receive_frame failed:" << ffmpeg_err(ret);
            break;
        }
        onDecode(out_frame);
    }
}

bool FFmpegDecoder::inputFrame_l(const Frame::Ptr &frame, bool live, bool enable_merge) {
    if (_do_merger && enable_merge) {
        return _merger.inputFrame(frame, [this, live](uint64_t dts, uint64_t pts, const Buffer::Ptr &buffer, bool have_idr) {
            decodeFrame(buffer->data(), buffer->size(), dts, pts, live, have_idr);
        });
    }

    return decodeFrame(frame->data(), frame->size(), frame->dts(), frame->pts(), live, frame->keyFrame());
}

bool FFmpegDecoder::inputFrame(const Frame::Ptr &frame, bool live, bool async, bool enable_merge) {
    if (async && !TaskManager::isEnabled() && getContext()->codec_type == AVMEDIA_TYPE_VIDEO) {
        // 开启异步编码，且为视频，尝试启动异步解码线程  [AUTO-TRANSLATED:17a68fc6]
        // Enable asynchronous encoding, and it is video, try to start asynchronous decoding thread
        startThread("decoder thread");
    }

    if (!async || !TaskManager::isEnabled()) {
        return inputFrame_l(frame, live, enable_merge);
    }

    auto frame_cache = Frame::getCacheAbleFrame(frame);
    return addDecodeTask(frame->keyFrame() || frame->configFrame(), [this, live, frame_cache, enable_merge]() {
        inputFrame_l(frame_cache, live, enable_merge);
        // 此处模拟解码太慢导致的主动丢帧  [AUTO-TRANSLATED:fc8bea8a]
        // Here simulates decoding too slow, resulting in active frame dropping
        //usleep(100 * 1000);
    });
}

bool FFmpegDecoder::decodeFrame(const char *data, size_t size, uint64_t dts, uint64_t pts, bool live, bool key_frame) {
    TimeTicker2(30, TraceL);

    auto pkt = alloc_av_packet();
    pkt->data = (uint8_t *) data;
    pkt->size = size;
    pkt->dts = dts;
    pkt->pts = pts;
    if (key_frame) {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    auto ret = avcodec_send_packet(_context.get(), pkt.get());
    if (ret < 0) {
        if (ret != AVERROR_INVALIDDATA) {
            WarnL << "avcodec_send_packet failed:" << ffmpeg_err(ret);
        }
        return false;
    }

    while (true) {
        auto out_frame = std::make_shared<FFmpegFrame>();
        ret = avcodec_receive_frame(_context.get(), out_frame->get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            WarnL << "avcodec_receive_frame failed:" << ffmpeg_err(ret);
            break;
        }
        if (live && pts - out_frame->get()->pts > MAX_DELAY_SECOND * 1000 && _ticker.createdTime() > 10 * 1000) {
            // 后面的帧才忽略,防止Track无法ready  [AUTO-TRANSLATED:23f1a7c9]
            // The following frames are ignored to prevent the Track from being ready
            WarnL << "解码时，忽略" << MAX_DELAY_SECOND << "秒前的数据:" << pts << " " << out_frame->get()->pts;
            continue;
        }
        onDecode(out_frame);
    }
    return true;
}

void FFmpegDecoder::setOnDecode(FFmpegDecoder::onDec cb) {
    _cb = std::move(cb);
}

void FFmpegDecoder::onDecode(const FFmpegFrame::Ptr &frame) {
    if (_cb) {
        _cb(frame);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

FFmpegAudioFifo::~FFmpegAudioFifo() {
    if (_fifo) {
        av_audio_fifo_free(_fifo);
        _fifo = nullptr;
    }
}

int FFmpegAudioFifo::size() const {
    return _fifo ? av_audio_fifo_size(_fifo) : 0;
}

bool FFmpegAudioFifo::Write(const AVFrame *frame) {
    _format = (AVSampleFormat)frame->format;
# if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1    
    _channels = frame->ch_layout.nb_channels;
#else
    _channels = frame->channels;
#endif
    if (!_fifo) {
        _fifo = av_audio_fifo_alloc(_format, _channels, frame->nb_samples);
        if (!_fifo) {
            WarnL << "av_audio_fifo_alloc " << _channels << "x" << frame->nb_samples << "error";
            return false;
        }
    }

    if (_samplerate != frame->sample_rate) {
        _samplerate = frame->sample_rate;
        // 假定传入frame的时间戳是以ms为单位的
        _timebase = 1000.0 / _samplerate;
    }
    if (frame->pts != AV_NOPTS_VALUE) {
        // 计算fifo audio第一个采样的时间戳
        double tsp = frame->pts - _timebase * av_audio_fifo_size(_fifo);
        // flv.js和webrtc对音频时间戳增量有要求, rtc要求更加严格！
        // 得尽量保证时间戳是按照sample_size累加，否则容易出现破音或杂音等问题
        if (fabs(_tsp) < DBL_EPSILON || fabs(tsp - _tsp) > 200) {
            InfoL << "reset base_tsp " << (int64_t)_tsp << "->" << (int64_t)tsp;
            _tsp = tsp;
        }
    } else {
        _tsp = 0;
    }

    av_audio_fifo_write(_fifo, (void **)frame->data, frame->nb_samples);
    return true;
}

bool FFmpegAudioFifo::Read(AVFrame *frame, int sample_size) {
    assert(_fifo);
    int fifo_size = av_audio_fifo_size(_fifo);
    if (fifo_size < sample_size)
        return false;
    // fill linedata
    av_samples_get_buffer_size(frame->linesize, _channels, sample_size, _format, 0);
    frame->nb_samples = sample_size;
    frame->format = _format;
#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
    av_channel_layout_default(&frame->ch_layout, _channels);
#else    
    frame->channel_layout = av_get_default_channel_layout(_channels);
#endif
    frame->sample_rate = _samplerate;
    if (fabs(_tsp) > DBL_EPSILON) {
        frame->pts = _tsp;
        // advance tsp by sample_size
        _tsp += sample_size * _timebase;
    }
    else {
        frame->pts = AV_NOPTS_VALUE;
    }

    int ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        WarnL << "av_frame_get_buffer error " << ffmpeg_err(ret);
        return false;
    }

    av_audio_fifo_read(_fifo, (void **)frame->data, sample_size);
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
FFmpegSwr::FFmpegSwr(AVSampleFormat output, AVChannelLayout *ch_layout, int samplerate) {
    _target_format = output;
    av_channel_layout_copy(&_target_ch_layout, ch_layout);
    _target_samplerate = samplerate;
}
#else
FFmpegSwr::FFmpegSwr(AVSampleFormat output, int channel, int channel_layout, int samplerate) {
    _target_format = output;
    _target_channels = channel;
    _target_channel_layout = channel_layout;
    _target_samplerate = samplerate;
}
#endif

FFmpegSwr::~FFmpegSwr() {
    if (_ctx) {
        swr_free(&_ctx);
    }
#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
    av_channel_layout_uninit(&_target_ch_layout);
#endif
}

FFmpegFrame::Ptr FFmpegSwr::inputFrame(const FFmpegFrame::Ptr &frame) {
    if (frame->get()->format == _target_format &&

#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
        !av_channel_layout_compare(&(frame->get()->ch_layout), &_target_ch_layout) &&
#else
        frame->get()->channels == _target_channels && frame->get()->channel_layout == (uint64_t)_target_channel_layout &&
#endif

        frame->get()->sample_rate == _target_samplerate) {
        // 不转格式  [AUTO-TRANSLATED:31dc6ae1]
        // Do not convert format
        return frame;
    }
    if (!_ctx) {

#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
        _ctx = swr_alloc();
        swr_alloc_set_opts2(&_ctx, 
                    &_target_ch_layout, _target_format, _target_samplerate, 
                    &frame->get()->ch_layout, (AVSampleFormat)frame->get()->format, frame->get()->sample_rate,
                     0, nullptr);
#else
        _ctx = swr_alloc_set_opts(nullptr, _target_channel_layout, _target_format, _target_samplerate,
                                  frame->get()->channel_layout, (AVSampleFormat) frame->get()->format,
                                  frame->get()->sample_rate, 0, nullptr);
#endif

        InfoL << "swr_alloc_set_opts:" << av_get_sample_fmt_name((enum AVSampleFormat) frame->get()->format) << " -> "
              << av_get_sample_fmt_name(_target_format);
    }
    if (_ctx) {
        auto out = std::make_shared<FFmpegFrame>();
        out->get()->format = _target_format;

#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
        out->get()->ch_layout = _target_ch_layout;
        av_channel_layout_copy(&(out->get()->ch_layout), &_target_ch_layout);
#else
        out->get()->channel_layout = _target_channel_layout;
        out->get()->channels = _target_channels;
#endif

        out->get()->sample_rate = _target_samplerate;
        out->get()->pkt_dts = frame->get()->pkt_dts;
        out->get()->pts = frame->get()->pts;

        int ret = 0;
        if (0 != (ret = swr_convert_frame(_ctx, out->get(), frame->get()))) {
            WarnL << "swr_convert_frame failed:" << ffmpeg_err(ret);
            return nullptr;
        }
        return out;
    }

    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

FFmpegSws::FFmpegSws(AVPixelFormat output, int width, int height) {
    _target_format = output;
    _target_width = width;
    _target_height = height;
}

FFmpegSws::~FFmpegSws() {
    if (_ctx) {
        sws_freeContext(_ctx);
        _ctx = nullptr;
    }
}

int FFmpegSws::inputFrame(const FFmpegFrame::Ptr &frame, uint8_t *data) {
    int ret;
    inputFrame(frame, ret, data);
    return ret;
}

FFmpegFrame::Ptr FFmpegSws::inputFrame(const FFmpegFrame::Ptr &frame) {
    int ret;
    return inputFrame(frame, ret, nullptr);
}

FFmpegFrame::Ptr FFmpegSws::inputFrame(const FFmpegFrame::Ptr &frame, int &ret, uint8_t *data) {
    ret = -1;
    TimeTicker2(30, TraceL);
    auto target_width = _target_width ? _target_width : frame->get()->width;
    auto target_height = _target_height ? _target_height : frame->get()->height;
    if (frame->get()->format == _target_format && frame->get()->width == target_width && frame->get()->height == target_height) {
        // 不转格式  [AUTO-TRANSLATED:31dc6ae1]
        // Do not convert format
        return frame;
    }
    if (_ctx && (_src_width != frame->get()->width || _src_height != frame->get()->height || _src_format != (enum AVPixelFormat) frame->get()->format)) {
        // 输入分辨率发生变化了  [AUTO-TRANSLATED:0e4ea2e8]
        // Input resolution has changed
        sws_freeContext(_ctx);
        _ctx = nullptr;
    }
    if (!_ctx) {
        _src_format = (enum AVPixelFormat) frame->get()->format;
        _src_width = frame->get()->width;
        _src_height = frame->get()->height;
        _ctx = sws_getContext(frame->get()->width, frame->get()->height, (enum AVPixelFormat) frame->get()->format, target_width, target_height, _target_format, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        InfoL << "sws_getContext:" << av_get_pix_fmt_name((enum AVPixelFormat) frame->get()->format) << " -> " << av_get_pix_fmt_name(_target_format);
    }
    if (_ctx) {
        auto out = std::make_shared<FFmpegFrame>();
        if (!out->get()->data[0]) {
            if (data) {
                av_image_fill_arrays(out->get()->data, out->get()->linesize, data, _target_format, target_width, target_height, 32);
            } else {
                out->fillPicture(_target_format, target_width, target_height);
            }
        }
        if (0 >= (ret = sws_scale(_ctx, frame->get()->data, frame->get()->linesize, 0, frame->get()->height, out->get()->data, out->get()->linesize))) {
            WarnL << "sws_scale failed:" << ffmpeg_err(ret);
            return nullptr;
        }

        out->get()->format = _target_format;
        out->get()->width = target_width;
        out->get()->height = target_height;
        out->get()->pkt_dts = frame->get()->pkt_dts;
        out->get()->pts = frame->get()->pts;
        return out;
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
template <int max_width = 4096, int max_height = 2160>
static void limitSize(int &width, int &height) {
    if (width > max_width) {
        auto target_width = MIN(width, max_width);
        height = height * target_width / width;
        width = target_width;
    }

    if (height > max_height) {
        auto target_height = MIN(height, max_height);
        width = width * target_height / height;
        height = target_height;
    }
}

FFmpegEncoder::FFmpegEncoder(const Track::Ptr &cfg, int thread_num, int format, const std::vector<std::string> &codec_name) {
    setupFFmpeg();
    const AVCodec *codec = nullptr;
    const AVCodec *codec_default = nullptr;
    _codecId = cfg->getCodecId();
    switch (_codecId) {
    case CodecH264:
        codec_default = getCodec<false>({ AV_CODEC_ID_H264 });
        if (codec_name.size()) {
            codec = getCodecByName<false>(codec_name);
        } else {
            GET_CONFIG_FUNC(std::vector<std::string>, h264EncList, kH264EncoderList, ToStrList);
            codec = getCodecByName<false>(h264EncList);
        }
        break;
    case CodecH265:
        codec_default = getCodec<false>({ AV_CODEC_ID_HEVC });
        if (codec_name.size()) {
            codec = getCodecByName<false>(codec_name);
        } else {
            GET_CONFIG_FUNC(std::vector<std::string>, h265EncList, kH265EncoderList, ToStrList);
            codec = getCodecByName<false>(h265EncList);
        }
        break;
    case CodecAAC:
        codec = getCodec<false>({ AV_CODEC_ID_AAC });
        break;
    case CodecG711A:
        codec = getCodec<false>({ AV_CODEC_ID_PCM_ALAW });
        break;
    case CodecG711U:
        codec = getCodec<false>({ AV_CODEC_ID_PCM_MULAW });
        break;
    case CodecOpus:
        codec = getCodec<false>({ AV_CODEC_ID_OPUS });
        break;
    case CodecJPEG:
        codec = getCodec<false>({ AV_CODEC_ID_MJPEG });
        break;
    case CodecVP8:
        codec = getCodec<false>({ AV_CODEC_ID_VP8 });
        break;
    case CodecVP9:
        codec = getCodec<false>({ AV_CODEC_ID_VP9 });
        break;
    default:
        break;
    }

    if (!codec) {
        throw std::runtime_error("未找到编码器");
    }

    while (true) {
        _context.reset(avcodec_alloc_context3(codec), [](AVCodecContext *ctx) { avcodec_free_context(&ctx); });

        if (!_context) {
            throw std::runtime_error("创建编码器失败");
        }

        switch (getTrackType()) {
            case TrackVideo: {
                auto video = (VideoTrack *)cfg.get();
                int fps = (int)video->getVideoFps();

                _context->codec_type = AVMEDIA_TYPE_VIDEO;
                _context->width = video->getVideoWidth();
                _context->height = video->getVideoHeight();
                limitSize(_context->width, _context->height);                
                _context->pix_fmt = (AVPixelFormat)selectVideoFormat(codec, format);

                _context->framerate = { fps, 1 };
                _context->time_base = { 1, 1000 };
                _context->bit_rate = cfg->getBitRate();
                _context->gop_size = fps * 2;
                _context->max_b_frames = 0;
                _context->has_b_frames = 0;
                break;
            }
            case TrackAudio: {
                auto audio = (AudioTrack *)cfg.get();
                int sample_rate = audio->getAudioSampleRate();
                int channels = audio->getAudioChannel();
                switch (_codecId) {
                    case CodecG711U:
                    case CodecG711A:
                        // g711只支持8K采样率
                        sample_rate = 8000;
                        channels = 1;
                        break;
                    default: break;
                }

                _context->codec_type = AVMEDIA_TYPE_AUDIO;
                _context->sample_fmt = (AVSampleFormat)selectAudioFormat(codec, format);
                _context->sample_rate = selectAudioSamplerate(codec, sample_rate);
#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
                av_channel_layout_default(&_context->ch_layout, channels);
#else
                _context->channels = channels;
                _context->channel_layout = av_get_default_channel_layout(channels);
#endif
                _context->bit_rate = cfg->getBitRate();
                _context->time_base = { 1, 1000 };
                _context->framerate = { sample_rate, 1 };
                break;
            }
            default: assert(0); break;
        }
        //保存AVFrame的引用
        //_context->refcounted_frames = 1;
        _context->flags |= AV_CODEC_FLAG_LOW_DELAY;
        _context->flags2 |= AV_CODEC_FLAG2_FAST;

        AVDictionary *dict = nullptr;
        if (thread_num <= 0) {
            av_dict_set(&dict, "threads", "auto", 0);
        } else {
            av_dict_set(&dict, "threads", to_string(MIN(thread_num, thread::hardware_concurrency())).data(), 0);
        }
        av_dict_set(&dict, "zerolatency", "1", 0);
        av_dict_set(&dict, "strict", "-2", 0);
        if (strcmp(codec->name, "libx264") == 0 || strcmp(codec->name, "libx265") == 0) {
            //设置为x265最快编码模型
            av_dict_set(&dict, "preset", "ultrafast", 0);
        }
        int ret = avcodec_open2(_context.get(), codec, &dict);
        av_dict_free(&dict);
        if (ret >= 0) {
            //成功
            InfoL << "打开编码器成功:" << codec->name;
            break;
        }
        if (codec_default && codec_default != codec) {
            //硬件编解码器打开失败，尝试软件的
            WarnL << "打开编码器" << codec->name << "失败，原因是:" << ffmpeg_err(ret) << ", 再尝试打开编码器" << codec_default->name;
            codec = codec_default;
            continue;
        }
        throw std::runtime_error(StrPrinter << "打开编码器" << codec->name << "失败:" << ffmpeg_err(ret));
    }
}

FFmpegEncoder::~FFmpegEncoder() {
    InfoL << getCodecName();
    stopThread(true);
    flush();
}

void FFmpegEncoder::flush() {
    while (true) {
        auto packet = alloc_av_packet();
        auto ret = avcodec_receive_packet(_context.get(), packet.get());
        if (ret == AVERROR(EAGAIN)) {
            avcodec_send_frame(_context.get(), nullptr);
            continue;
        }
        if (ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            WarnL << "avcodec_receive_frame failed:" << ffmpeg_err(ret);
            break;
        }
        onEncode(packet.get());
    }
}

bool FFmpegEncoder::inputFrame(const FFmpegFrame::Ptr &frame, bool async) {
    if (async && !TaskManager::isEnabled() && getContext()->codec_type == AVMEDIA_TYPE_VIDEO) {
        //开启异步编码，且为视频，尝试启动异步解码线程
        startThread("encoder thread");
    }

    if (!async || !TaskManager::isEnabled()) {
        inputFrame_l(frame);
        return true;
    }

    return addEncodeTask([this, frame]() { inputFrame_l(frame); });
}

void FFmpegEncoder::inputFrame_l(FFmpegFrame::Ptr frame_in) {
    FFmpegFrame::Ptr frame = frame_in;
    if (getTrackType() == TrackVideo) {
        //视频需要转格式再编码
        if (!_sws) {
            _sws.reset(new FFmpegSws(_context->pix_fmt, _context->width, _context->height));
        }
        if (!_sws) {
            InfoL << "创建swscale对象失败";
            return;
        }
        frame = _sws->inputFrame(frame_in);
    } else {
        if (!_swr) {
#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
            _swr.reset(new FFmpegSwr(_context->sample_fmt, &_context->ch_layout, _context->sample_rate));
#else
            _swr.reset(new FFmpegSwr(_context->sample_fmt, _context->channels, _context->channel_layout, _context->sample_rate));
#endif
        }
        if (!_swr) {
            InfoL << "创建swresample对象失败";
            return;
        }
        frame = _swr->inputFrame(frame_in);
    }

    if (!frame) {
        return;
    }

    if (getTrackType() == TrackVideo || _context->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) {
        encodeFrame(frame->get());
        return;
    }

    if (!_fifo)
        _fifo.reset(new FFmpegAudioFifo());
    _fifo->Write(frame->get());
    std::shared_ptr<AVFrame> fifo_out(av_frame_alloc(), [](AVFrame *frame) { av_frame_free(&frame); });
    while (true) {
        auto ptr = _fifo->Read(fifo_out.get(), _context->frame_size);
        if (!ptr) {
            return;
        }
        encodeFrame(fifo_out.get());
    }
}

bool FFmpegEncoder::encodeFrame(AVFrame *frame) {
    // TraceL << "enc " << frame->pts;
    int ret = avcodec_send_frame(_context.get(), frame);
    if (ret < 0) {
        WarnL << "Error sending a frame " << frame->pts << " to the encoder: " << ffmpeg_err(ret);
        return false;
    }
    while (ret >= 0) {
        auto packet = alloc_av_packet();
        ret = avcodec_receive_packet(_context.get(), packet.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            WarnL << "Error encoding a frame: " << ffmpeg_err(ret);
            return false;
        }
        // TraceL << "out " << packet->pts << "," << packet->dts << ", size: " << packet->size;
        onEncode(packet.get());
    }
    return true;
}

#define ADTS_HEADER_LEN 7
extern int dumpAacConfig(const string &config, size_t length, uint8_t *out, size_t out_size);
void FFmpegEncoder::onEncode(AVPacket *packet) {
    // process frame
    if (!_cb)
        return;
    if (_codecId == CodecAAC) {
        auto frame = FrameImp::create<>();
        frame->_codec_id = _codecId;
        frame->_dts = packet->dts;
        frame->_pts = packet->pts;
        frame->_buffer.reserve(ADTS_HEADER_LEN + packet->size);
        if (_context && _context->extradata && _context->extradata_size) {
            uint8_t adts[ADTS_HEADER_LEN];
            auto cfg = std::string((const char *)_context->extradata, _context->extradata_size);
            dumpAacConfig(cfg, packet->size, adts, ADTS_HEADER_LEN);
            frame->_prefix_size = ADTS_HEADER_LEN;
            frame->_buffer.append((char*)adts, ADTS_HEADER_LEN);
        }
        frame->_buffer.append((const char *)packet->data, packet->size);
        _cb(frame);
    } else {
        _cb(Factory::getFrameFromPtr(_codecId, (const char*)packet->data, packet->size, packet->dts, packet->pts));
    }
}

} //namespace mediakit
#endif//ENABLE_FFMPEG
