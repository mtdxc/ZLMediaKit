/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifdef ENABLE_MP4

#include <inttypes.h>
#include <algorithm>
#include "MP4Demuxer.h"
#include "MP4Muxer.h"
#include "Util/File.h"
#include "Util/logger.h"
#include "Extension/Factory.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

MP4Demuxer::~MP4Demuxer() {
    closeMP4();
}

void MP4Demuxer::openMP4(const string &file) {
    closeMP4();

    _mp4_file = std::make_shared<MP4FileDisk>();
    _mp4_file->openFile(file.data(), "rb+");
    if (file.find(".webm") != std::string::npos) {
        _mkv_reader = _mp4_file->createWebmReader();
        _duration_ms = mkv_reader_getduration(_mkv_reader.get());
    } else {
        _mov_reader = _mp4_file->createReader();
        _duration_ms = mov_reader_getduration(_mov_reader.get());
    }
    getAllTracks();
}

void MP4Demuxer::closeMP4() {
    _mov_reader.reset();
    _mp4_file.reset();
    _mkv_reader.reset();
}

int MP4Demuxer::getAllTracks() {
    static mov_reader_trackinfo_t s_on_track = {
            [](void *param, uint32_t track, uint8_t object, int width, int height, const void *extra, size_t bytes) {
                //onvideo
                MP4Demuxer *thiz = (MP4Demuxer *)param;
                thiz->onVideoTrack(track,object,width,height,extra,bytes);
            },
            [](void *param, uint32_t track, uint8_t object, int channel_count, int bit_per_sample, int sample_rate, const void *extra, size_t bytes) {
                //onaudio
                MP4Demuxer *thiz = (MP4Demuxer *)param;
                thiz->onAudioTrack(track,object,channel_count,bit_per_sample,sample_rate,extra,bytes);
            },
            [](void *param, uint32_t track, uint8_t object, const void *extra, size_t bytes) {
                //onsubtitle, do nothing
            }
    };
    static mkv_reader_trackinfo_t w_on_track = {
            [](void *param, uint32_t track, mkv_codec_t object, int width, int height, const void *extra, size_t bytes) {
                //onvideo
                MP4Demuxer *thiz = (MP4Demuxer *)param;
                thiz->onVideoTrack(track,object,width,height,extra,bytes);
            },
            [](void *param, uint32_t track, mkv_codec_t object, int channel_count, int bit_per_sample, int sample_rate, const void *extra, size_t bytes) {
                //onaudio
                MP4Demuxer *thiz = (MP4Demuxer *)param;
                thiz->onAudioTrack(track,object,channel_count,bit_per_sample,sample_rate,extra,bytes);
            },
            [](void *param, uint32_t track, mkv_codec_t object, const void *extra, size_t bytes) {
                //onsubtitle, do nothing
            }
    };
    if (_mov_reader)
        return mov_reader_getinfo(_mov_reader.get(),&s_on_track,this);
    if (_mkv_reader)
        return mkv_reader_getinfo(_mkv_reader.get(), &w_on_track, this);
    return 0;
}

void MP4Demuxer::onVideoTrack(uint32_t track, int object, int width, int height, const void *extra, size_t bytes) {
    auto video = Factory::getTrackByCodecId(_mov_reader?getCodecByMovId(object):getCodecByMkvId(object));
    if (!video) {
        return;
    }
    video->setIndex(track);
    _tracks.emplace(track, video);
    if (extra && bytes) {
        video->setExtraData((uint8_t *)extra, bytes);
    }
}

void MP4Demuxer::onAudioTrack(uint32_t track, int object, int channel_count, int bit_per_sample, int sample_rate, const void *extra, size_t bytes) {
    auto audio = Factory::getTrackByCodecId(_mov_reader?getCodecByMovId(object):getCodecByMkvId(object), sample_rate, channel_count, bit_per_sample / channel_count);
    if (!audio) {
        return;
    }
    audio->setIndex(track);
    _tracks.emplace(track, audio);
    if (extra && bytes) {
        audio->setExtraData((uint8_t *)extra, bytes);
    }
}

int64_t MP4Demuxer::seekTo(int64_t stamp_ms) {
    if (_mov_reader) {
        if (0 != mov_reader_seek(_mov_reader.get(), &stamp_ms))
            return -1;
    }
    if (_mkv_reader) {
        if (0 != mkv_reader_seek(_mkv_reader.get(), &stamp_ms))
            return -1;
    }
    return stamp_ms;
}

struct Context {
    Context(MP4Demuxer *ptr) : thiz(ptr) {}
    MP4Demuxer *thiz;
    int flags = 0;
    int64_t pts = 0;
    int64_t dts = 0;
    uint32_t track_id = 0;
    BufferRaw::Ptr buffer;
};

Frame::Ptr MP4Demuxer::readFrame(bool &keyFrame, bool &eof) {
    keyFrame = false;
    eof = false;

    static mov_reader_onread2 mov_onalloc = [](void *param, uint32_t track_id, size_t bytes, int64_t pts, int64_t dts, int flags) -> void * {
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
    int ret = 0;
    if (_mov_reader)
        ret = mov_reader_read2(_mov_reader.get(), mov_onalloc, &ctx);
    if (_mkv_reader)
        ret = mkv_reader_read2(_mkv_reader.get(), mov_onalloc, &ctx);
    switch (ret) {
        case 0 : {
            eof = true;
            return nullptr;
        }

        case 1 : {
            keyFrame = ctx.flags & MOV_AV_FLAG_KEYFREAME;
            return makeFrame(ctx.track_id, ctx.buffer, ctx.pts, ctx.dts);
        }

        default : {
            eof = true;
            WarnL << "读取mp4文件数据失败:" << ret;
            return nullptr;
        }
    }
}

Frame::Ptr MP4Demuxer::makeFrame(uint32_t track_id, Buffer::Ptr buf, int64_t pts, int64_t dts) {
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

vector<Track::Ptr> MP4Demuxer::getTracks(bool ready) const {
    vector<Track::Ptr> ret;
    for (auto &pr : _tracks) {
        if (ready && !pr.second->ready()) {
            continue;
        }
        ret.push_back(pr.second);
    }
    return ret;
}

uint64_t MP4Demuxer::getDurationMS() const {
    return _duration_ms;
}

/////////////////////////////////////////////////////////////////////////////////

void MultiMP4Demuxer::openMP4(const string &files_string) {
    std::vector<std::string> files;
    if (File::is_dir(files_string)) {
        File::scanDir(files_string, [&](const string &path, bool is_dir) {
            if (!is_dir && end_with(path, ".mp4")) {
                files.emplace_back(path);
            }
            return true;
        }, true);
        std::sort(files.begin(), files.end());
    } else {
        files = split(files_string, ";");
    }

    uint64_t duration_ms = 0;
    for (auto &file : files) {
        auto demuxer = std::make_shared<MP4Demuxer>();
        demuxer->openMP4(file);
        _demuxers.emplace(duration_ms, demuxer);
        duration_ms += demuxer->getDurationMS();
    }
    CHECK(!_demuxers.empty());
    _it = _demuxers.begin();
    for (auto &track : _it->second->getTracks(false)) {
        auto clone_track(track->clone());
        clone_track->setIndex(clone_track->getTrackType());
        _tracks.emplace(clone_track->getIndex(), clone_track);
        DebugL << "track index: " << track->getIndex() << " -> " << clone_track->getIndex();
    }
}

uint64_t MultiMP4Demuxer::getDurationMS() const {
    return _demuxers.empty() ? 0 : _demuxers.rbegin()->first + _demuxers.rbegin()->second->getDurationMS();
}

void MultiMP4Demuxer::closeMP4() {
    _demuxers.clear();
    _it = _demuxers.end();
    _tracks.clear();
}

int64_t MultiMP4Demuxer::seekTo(int64_t stamp_ms) {
    if (stamp_ms >= (int64_t)getDurationMS()) {
        return -1;
    }
    _it = std::prev(_demuxers.upper_bound(stamp_ms));
    return _it->first + _it->second->seekTo(stamp_ms - _it->first);
}

Frame::Ptr MultiMP4Demuxer::readFrame(bool &keyFrame, bool &eof) {
    for (;;) {
        auto ret = _it->second->readFrame(keyFrame, eof);
        if (ret) {
            ret->setIndex(ret->getTrackType());
            auto it = _tracks.find(ret->getIndex());
            if (it != _tracks.end()) {
                auto ret2 = std::make_shared<FrameStamp>(ret);
                ret2->setStamp(_it->first + ret->dts(), _it->first + ret->pts());
                ret = std::move(ret2);
                it->second->inputFrame(ret);
            }
        }
        if (eof && _it != _demuxers.end()) {
            // 切换到下一个文件
            if (++_it == _demuxers.end()) {
                // 已经是最后一个文件了
                eof = true;
                return nullptr;
            }
            // 下一个文件从头开始播放
            _it->second->seekTo(0);
            continue;
        }
        return ret;
    }
}

std::vector<Track::Ptr> MultiMP4Demuxer::getTracks(bool trackReady) const {
    std::vector<Track::Ptr> ret;
    for (auto &pr : _tracks) {
        if (!trackReady || pr.second->ready()) {
            ret.emplace_back(pr.second);
        }
    }
    return ret;
}

void mp4Dump(const std::string &path, TrackType type) {
    try {
        MP4Demuxer src;
        src.openMP4(path);
        printf(">>> file:%s\n", path.c_str());
        printf("duration %" PRIu64 " s\n", src.getDurationMS() / 1000);
        for (auto &track : src.getTracks(true)) {
            if (type == TrackMax || type == TrackInvalid || track->getTrackType() == type) {
                printf("track %d: %s\n", track->getIndex(), track->getInfo().c_str());
            }
        }
        if (type == TrackInvalid) {
            return;
        }
        bool key = false;
        bool eof = false;
        while (!eof) {
            auto frame = src.readFrame(key, eof);
            if (!frame)
                break;
            if (type == TrackMax || frame->getTrackType() == type) {
                printf("frame %d, %s %4zu tsp %" PRIu64 ",%" PRIu64 " %d%s\n", 
                  frame->getIndex(), frame->getCodecName(), frame->size(), 
                  frame->pts(), frame->dts(), key,
                  frame->keyFrame() ? " key" : "");
            }
        }
    } catch (std::exception &ex) {
        WarnL << ex.what();
    }
}

uint64_t copyMp4(MP4Demuxer &src, MP4Muxer &dst, TrackType type) {
    std::vector<Track::Ptr> vecs;
    for (auto t : src.getTracks(true)) {
        if (t->getTrackType() == type || type == TrackMax) {
            vecs.push_back(t);
        }
    }
    if (vecs.empty()) {
        InfoL << "no tracks " << getTrackString(type);
        return 0;
    }

    toolkit::Ticker tick;
    for (auto t : vecs) {
        dst.addTrack(t);
        t->addDelegate([&dst](const Frame::Ptr &frame) { 
          dst.inputFrame(frame);
          return true;
        });
    }
    dst.addTrackCompleted();
    // 禁用时间戳修改
    dst.setPlayback();
    // support fmp4 segment
    dst.initSegment();

    bool key = false;
    bool eof = false;
    while (src.readFrame(key, eof)) {
    }

    uint64_t ret = dst.getDuration();
    auto timeMs = tick.elapsedTime();
    if (timeMs) {
        InfoL << "tooks " << timeMs << " ms, speed=" << ret * 1.0 / timeMs << "x, diff=" << ((int64_t)src.getDurationMS() - (int64_t)ret) << " ms";
    }
    return ret;
}

uint64_t copyMp4(const std::string &srcPath, const std::string &dstPath, int flag, int type) {
    MP4Demuxer src;
    src.openMP4(srcPath);
    InfoL << srcPath << " -> " << dstPath << ", durationMs=" << src.getDurationMS();

    MP4Muxer dst;
    dst.openMP4(dstPath, flag, type);
    return copyMp4(src, dst, TrackMax);
}

uint64_t copyMp4Raw(const std::string &srcPath, const std::string &dstPath, int flag, TrackType track) {
    toolkit::Ticker tick;
    auto srcFile = std::make_shared<MP4FileDisk>();
    srcFile->openFile(srcPath.data(), "rb+");
    auto srcReader = srcFile->createReader();

    auto dstFile = std::make_shared<MP4FileDisk>();
    dstFile->openFile(dstPath.data(), "wb+");

    struct CopyCtx {
        MP4FileIO::Writer writer;
        std::map<int, int> track_map;
        TrackType type;
        // copy context for onalloc callback
        std::vector<uint8_t> buffer;
        uint32_t track_id;
        size_t bytes;
        int64_t pts, dts;
        int flags;
    };
    auto dst = std::make_shared<CopyCtx>();
    int type = dstPath.find(".fmp4") != std::string::npos ? 1 : 0;
    dst->writer = dstFile->createWriter(flag, type);
    dst->type = track;

    static mov_reader_trackinfo_t s_on_track
        = { [](void *param, uint32_t track, uint8_t object, int width, int height, const void *extra, size_t bytes) {
               // onvideo
               CopyCtx *ctx = (CopyCtx *)param;
               if (ctx->type == TrackAudio || ctx->type == TrackMax) {
                   ctx->track_map[track] = mp4_writer_add_video(ctx->writer.get(), getCodecByMovId(object), width, height, extra, bytes);
               }
           },
            [](void *param, uint32_t track, uint8_t object, int channel_count, int bit_per_sample, int sample_rate, const void *extra, size_t bytes) {
                // onaudio
                CopyCtx *ctx = (CopyCtx *)param;
                if (ctx->type == TrackVideo || ctx->type == TrackMax) {
                    ctx->track_map[track] = mp4_writer_add_audio(ctx->writer.get(), getCodecByMovId(object), channel_count, bit_per_sample, sample_rate, extra, bytes);
                }
            },
            [](void *param, uint32_t track, uint8_t object, const void *extra, size_t bytes) {
                // onsubtitle, do nothing
            } };
    mov_reader_getinfo(srcReader.get(), &s_on_track, dst.get());
    mp4_writer_init_segment(dst->writer.get());
    uint64_t duration_ms = mov_reader_getduration(srcReader.get());
    InfoL << srcPath << " -> " << dstPath << ", durationMs=" << duration_ms << ", openms=" << tick.elapsedTime();

    static mov_reader_onread2 mov_onalloc = [](void *param, uint32_t track_id, size_t bytes, int64_t pts, int64_t dts, int flags) -> void * {
        CopyCtx *ctx = (CopyCtx *)param;
        if (ctx->buffer.size() < bytes) {
            ctx->buffer.resize(bytes + 1);
        }
        ctx->bytes = bytes;
        ctx->track_id = track_id;
        ctx->pts = pts;
        ctx->dts = dts;
        ctx->flags = flags;
        return ctx->buffer.data();
    };
    while (1 == mov_reader_read2(srcReader.get(), mov_onalloc, dst.get())) {
        auto it = dst->track_map.find(dst->track_id);
        if (it != dst->track_map.end()) {
            mp4_writer_write(dst->writer.get(), it->second, dst->buffer.data(), dst->bytes, dst->pts, dst->dts, dst->flags);
        }
    }
    auto timeMs = tick.elapsedTime();
    if (timeMs) {
        InfoL << "tooks " << timeMs << " ms, speed=" << duration_ms * 1.0 / timeMs << "x";
    }
    return duration_ms;
}

uint64_t splitMp4(const std::string &srcPath, const std::string &dstPath, TrackType type) {
    MP4Demuxer src;
    src.openMP4(srcPath);
    InfoL << srcPath << " -> " << dstPath << ", durationMs=" << src.getDurationMS() << ", track type: " << getTrackString(type);

    MP4Muxer dst;
    dst.openMP4(dstPath);
    return copyMp4(src, dst, type);
}

int Mp4DropVideo(const char *srcPath, const char *dstPath, uint64_t start, uint64_t end) {
    MP4Demuxer src;
    MP4Muxer dst;
    src.openMP4(srcPath);
    dst.openMP4(dstPath);
    if (!end)
        end = src.getDurationMS();

    CodecId videoId = CodecInvalid;
    auto tracks = src.getTracks(true);
    for (auto track : tracks) {
        if (track->getTrackType() == TrackVideo)
            videoId = track->getCodecId();
        dst.addTrack(track);
    }
    if (videoId == CodecInvalid) {
        printf("no need to convert\n");
        return 0;
    }
    dst.addTrackCompleted();
    // 禁用时间戳修改
    dst.setPlayback();

    int drop = 0;
    bool key, eof = false;
    while (!eof) {
        auto frame = src.readFrame(key, eof);
        if (!frame)
            break;
        if (frame->getCodecId() == videoId) {
            if (frame->pts() >= start && frame->pts() < end) {
                if (!drop) {
                    InfoL << "begin drop with tsp " << frame->pts();
                }
                drop++;
                continue;
            } else if (drop) {
                if (key) {
                    InfoL << "end drop " << drop << " with tsp " << frame->pts();
                    drop = 0;
                } else {
                    drop++;
                    continue;
                }
            }
        }
        dst.inputFrame2(frame);
    }
    if (drop) {
        InfoL << "end drop " << drop << " at eof";
    }
    return 0;
}
}//namespace mediakit
#endif// ENABLE_MP4
