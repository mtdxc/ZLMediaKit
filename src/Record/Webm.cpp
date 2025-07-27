#include "Webm.h"
#include "Extension/Factory.h"
#include <iostream>
#include "Util/logger.h"
#include "Util/File.h"
#include "mkv-reader.h"
#include "mkv-writer.h"
#include "mkv-format.h"
using namespace toolkit;
namespace mediakit {

WebmMuxer::WebmMuxer() {
}

WebmMuxer::~WebmMuxer() {
    close();
}

void WebmMuxer::close() {
    resetTracks();
    if (_context) {
        mkv_writer_destroy(_context);
        _context = nullptr;
    }
    if (_file) {
        fclose(_file);
        _file = nullptr;
    }
}

void WebmMuxer::resetTracks() {
    _tracks.clear();
    _have_video = false;
    _started = false;
    open(_file_path);
}

bool WebmMuxer::open(const std::string &file) {
    _file = File::create_file(file, "wb");
    if (!_file) {
        WarnL << "Failed to open file: " << file;
        return false;
    }
    _context = mkv_writer_create(mkv_file_buffer(), _file, MKV_OPTION_WEBM);
    if (!_context) {
        fclose(_file);
        _file = nullptr;
        WarnL << "Failed to create MKV writer context";
        return false;
    }
    _file_path = file;
    return true;
}

bool WebmMuxer::addTrack(const Track::Ptr &track) {
    int cid = getMkvIdByCodec(track->getCodecId());
    if (cid == MKV_CODEC_UNKNOWN) {
        WarnL << "Unsupported codec: " << track->getCodecName();
        return false;
    }
    auto extra = track->getExtraData();
    if (track->getTrackType() == TrackVideo) {
        _have_video = true;
        auto video_track = std::dynamic_pointer_cast<VideoTrack>(track);
        if (!video_track) {
            WarnL << "Track is not a VideoTrack: " << track->getCodecName();
            return false;
        }
        _tracks[track->getIndex()].track_id = mkv_writer_add_video(_context, (mkv_codec_t)cid, 
            video_track->getVideoWidth(), video_track->getVideoHeight(), 
            extra ? extra->data() : nullptr, extra ? extra->size() : 0);
    }
    else{
        auto audio_track = std::dynamic_pointer_cast<AudioTrack>(track);
        if (!audio_track) {
            WarnL << "Track is not an AudioTrack: " << track->getCodecName();
            return false;
        }
        _tracks[track->getIndex()].track_id = mkv_writer_add_audio(_context, (mkv_codec_t)cid, 
            audio_track->getAudioChannel(), audio_track->getAudioSampleBit(), audio_track->getAudioSampleRate(),
            extra ? extra->data() : nullptr, extra ? extra->size() : 0);
    }
    return true;
}

bool WebmMuxer::inputFrame(const Frame::Ptr &frame) {
    auto it = _tracks.find(frame->getIndex());
    if (it == _tracks.end()) {
        return false;
    }
    if (!_started) {
        // 该逻辑确保含有视频时，第一帧为关键帧
        if (_have_video && !frame->keyFrame()) {
            // 含有视频，但是不是关键帧，那么前面的帧丢弃
            return false;
        }
        // 开始写文件
        _started = true;
    }

    auto &track = it->second;
    switch (frame->getCodecId()) {
        case CodecH264:
        case CodecH265: {
            // 这里的代码逻辑是让SPS、PPS、IDR这些时间戳相同的帧打包到一起当做一个帧处理
            return track.merger.inputFrame(frame, [this, &track](uint64_t dts, uint64_t pts, const Buffer::Ptr &buffer, bool have_idr) {
                // 取视频时间戳为TS的时间戳
                int64_t dts_out, pts_out;
                track.stamp.revise(dts, pts, dts_out, pts_out);
                mkv_writer_write(_context, track.track_id, buffer->data(), buffer->size(), pts_out, dts_out, have_idr ? MKV_FLAGS_KEYFRAME : 0);
            });
        }

        case CodecAAC: {
            //CHECK(frame->prefixSize(), "Mpeg muxer required aac frame with adts heade");
        }

        default: {
            int64_t dts_out, pts_out;
            track.stamp.revise(frame->dts(), frame->pts(), dts_out, pts_out);
            mkv_writer_write(_context, track.track_id, 
                frame->data() + frame->prefixSize(), frame->size() - frame->prefixSize(), 
                pts_out, dts_out,
                frame->keyFrame() ? MKV_FLAGS_KEYFRAME : 0);
            return true;
        }
    }
}

uint64_t WebmMuxer::getDuration() const {
    uint64_t ret = 0;
    for (auto &pr : _tracks) {
        if (pr.second.stamp.getRelativeStamp() > (int64_t)ret) {
            ret = pr.second.stamp.getRelativeStamp();
        }
    }
    return ret;
}


WebmDemuxer::~WebmDemuxer() {
    close();
}

bool WebmDemuxer::open(const std::string &file) {
    close();
    _file = File::create_file(file, "rb");
    if (!_file) {
        WarnL << "Failed to open file: " << file;
        return false;
    }
    _context = mkv_reader_create(mkv_file_buffer(), _file);
    if (!_context) {
        fclose(_file);
        _file = nullptr;
        WarnL << "Failed to open file: " << file;
        return false;
    }
    getAllTracks();
    _duration_ms = mkv_reader_getduration(_context);
    return true;
}

void WebmDemuxer::close() {
    if (_context) {
        mkv_reader_destroy(_context);
        _context = nullptr;
    }
    if (_file) {
        fclose(_file);
        _file = nullptr;
    }
    _tracks.clear();
    _duration_ms = 0;
}

int WebmDemuxer::getAllTracks() {
    static mkv_reader_trackinfo_t s_on_track = {
        [](void *param, uint32_t track, mkv_codec_t object, int width, int height, const void *extra, size_t bytes) {
            //onvideo
            WebmDemuxer *thiz = (WebmDemuxer *)param;
            thiz->onVideoTrack(track,(uint8_t)object,width,height,extra,bytes);
        },
        [](void *param, uint32_t track, mkv_codec_t object, int channel_count, int bit_per_sample, int sample_rate, const void *extra, size_t bytes) {
            //onaudio
            WebmDemuxer *thiz = (WebmDemuxer *)param;
            thiz->onAudioTrack(track,(uint8_t)object,channel_count,bit_per_sample,sample_rate,extra,bytes);
        },
        [](void *param, uint32_t track, mkv_codec_t object, const void *extra, size_t bytes) {
            //onsubtitle, do nothing
        }
    };
    return mkv_reader_getinfo(_context, &s_on_track,this);
}

void WebmDemuxer::onVideoTrack(uint32_t track, int object, int width, int height, const void *extra, size_t bytes) {
    auto video = Factory::getTrackByCodecId(getCodecByMkvId(object));
    if (!video) {
        return;
    }
    video->setIndex(track);
    _tracks.emplace(track, video);
    if (extra && bytes) {
        video->setExtraData((uint8_t *)extra, bytes);
    }
}

void WebmDemuxer::onAudioTrack(uint32_t track, int object, int channel_count, int bit_per_sample, int sample_rate, const void *extra, size_t bytes) {
    auto audio = Factory::getTrackByCodecId(getCodecByMkvId(object), sample_rate, channel_count, bit_per_sample / channel_count);
    if (!audio) {
        return;
    }
    audio->setIndex(track);
    _tracks.emplace(track, audio);
    if (extra && bytes) {
        audio->setExtraData((uint8_t *)extra, bytes);
    }
}

int64_t WebmDemuxer::seekTo(int64_t stamp_ms) {
    if(0 != mkv_reader_seek(_context, &stamp_ms)){
        return -1;
    }
    return stamp_ms;
}

struct Context {
    Context(WebmDemuxer *ptr) : thiz(ptr) {}
    WebmDemuxer *thiz;
    int flags = 0;
    int64_t pts = 0;
    int64_t dts = 0;
    uint32_t track_id = 0;
    BufferRaw::Ptr buffer;
};

Frame::Ptr WebmDemuxer::readFrame(bool &keyFrame, bool &eof) {
    keyFrame = false;
    eof = false;

    static mkv_reader_onread2 mov_onalloc = [](void *param, uint32_t track_id, size_t bytes, int64_t pts, int64_t dts, int flags) -> void * {
        Context *ctx = (Context *) param;
        ctx->pts = pts;
        ctx->dts = dts;
        ctx->flags = flags;
        ctx->track_id = track_id;

        ctx->buffer = ctx->thiz->_buffer_pool.obtain2();
        ctx->buffer->setCapacity(bytes + 1);
        ctx->buffer->setSize(bytes);
        return ctx->buffer->data();
    };

    Context ctx(this);
    auto ret = mkv_reader_read2(_context, mov_onalloc, &ctx);
    switch (ret) {
        case 0 : {
            eof = true;
            return nullptr;
        }

        case 1 : {
            keyFrame = ctx.flags & MKV_FLAGS_KEYFRAME;
            return makeFrame(ctx.track_id, ctx.buffer, ctx.pts, ctx.dts);
        }

        default : {
            eof = true;
            WarnL << "读取webm文件数据失败:" << ret;
            return nullptr;
        }
    }
}

Frame::Ptr WebmDemuxer::makeFrame(uint32_t track_id, const Buffer::Ptr &buf, int64_t pts, int64_t dts) {
    auto it = _tracks.find(track_id);
    if (it == _tracks.end()) {
        return nullptr;
    }
    Frame::Ptr ret;
    auto codec = it->second->getCodecId();
    switch (codec) {
        case CodecH264:
        case CodecH265: {
            auto bytes = buf->size();
            auto data = buf->data();
            auto offset = 0u;
            while (offset < bytes) {
                uint32_t frame_len;
                memcpy(&frame_len, data + offset, 4);
                frame_len = ntohl(frame_len);
                if (frame_len + offset + 4 > bytes) {
                    return nullptr;
                }
                memcpy(data + offset, "\x00\x00\x00\x01", 4);
                offset += (frame_len + 4);
            }
            ret = Factory::getFrameFromBuffer(codec, std::move(buf), dts, pts);
            break;
        }

        default: {
            ret = Factory::getFrameFromBuffer(codec, std::move(buf), dts, pts);
            break;
        }
    }
    if (ret) {
        ret->setIndex(track_id);
        it->second->inputFrame(ret);
    }
    return ret;
}

std::vector<Track::Ptr> WebmDemuxer::getTracks(bool ready) const {
    std::vector<Track::Ptr> ret;
    for (auto &pr : _tracks) {
        if (ready && !pr.second->ready()) {
            continue;
        }
        ret.push_back(pr.second);
    }
    return ret;
}
}