﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */
#ifdef _WIN32
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif
#include "Frame.h"
#include <inttypes.h>
#include "Common/Parser.h"
#include "Common/Stamp.h"

using namespace std;
using namespace toolkit;

namespace toolkit {
    StatisticImp(mediakit::Frame);
    StatisticImp(mediakit::FrameImp);
}

namespace mediakit{

std::string Frame::dump() const{
    char line[256];
    sprintf(line, "%s pts:%" PRIu64 " dts:%" PRIu64 " size:%3d %s%s", 
        getCodecName(), pts(), dts(), (int)size(), 
        keyFrame()?"key":"", configFrame()?"config":"");
    return line;
}

Frame::Ptr Frame::getCacheAbleFrame(const Frame::Ptr &frame){
    if(frame->cacheAble()){
        return frame;
    }
    return std::make_shared<FrameCacheAble>(frame);
}

FrameStamp::FrameStamp(Frame::Ptr frame, Stamp &stamp, bool modify_stamp)
{
    _frame = std::move(frame);
    //覆盖时间戳
    stamp.revise(_frame->dts(), _frame->pts(), _dts, _pts, modify_stamp);
}

TrackType getTrackType(CodecId codecId) {
    switch (codecId) {
#define XX(name, type, value, str, mpeg_id) case name : return type;
        CODEC_MAP(XX)
#undef XX
        default : return TrackInvalid;
    }
}

const char *getCodecName(CodecId codec) {
    switch (codec) {
#define XX(name, type, value, str, mpeg_id) case name : return str;
        CODEC_MAP(XX)
#undef XX
        default : return "invalid";
    }
}

#define XX(name, type, value, str, mpeg_id) {str, name},
static std::map<std::string, CodecId, StrCaseCompare> codec_map = {CODEC_MAP(XX)};
#undef XX

CodecId getCodecId(const std::string &str){
    auto it = codec_map.find(str);
    return it == codec_map.end() ? CodecInvalid : it->second;
}

static std::map<std::string, TrackType, StrCaseCompare> track_str_map = {
        {"video",       TrackVideo},
        {"audio",       TrackAudio},
        {"application", TrackApplication}
};

TrackType getTrackType(const std::string &str) {
    auto it = track_str_map.find(str);
    return it == track_str_map.end() ? TrackInvalid : it->second;
}

const char* getTrackString(TrackType type){
    switch (type) {
        case TrackVideo : return "video";
        case TrackAudio : return "audio";
        case TrackApplication : return "application";
        default: return "invalid";
    }
}

const char *CodecInfo::getCodecName() const {
    return mediakit::getCodecName(getCodecId());
}

TrackType CodecInfo::getTrackType() const {
    return mediakit::getTrackType(getCodecId());
}

static size_t constexpr kMaxFrameCacheSize = 100;

bool FrameMerger::willFlush(const Frame::Ptr &frame) const{
    if (_frame_cache.empty()) {
        //缓存为空
        return false;
    }
    else if (_frame_cache.size() > kMaxFrameCacheSize) {
        // 缓存太多，防止内存溢出，强制flush输出
        InfoL << "帧缓存过多:" << _frame_cache.size() << "，强制刷新";
        return true;
    }
    if (!frame) {
        InfoL << "flush with empty frame";
        return true;
    } else if (_frame_cache.back()->dts() != frame->dts()) {
        // 遇到新帧
        return true;
    }
    switch (_type) {
        case none : {
            //frame不是完整的帧，我们合并为一帧
            bool new_frame = false;
            switch (frame->getCodecId()) {
                case CodecH264:
                case CodecH265:
                    //如果是新的一帧，前面的缓存需要输出
                    new_frame = frame->prefixSize();
                    break;
                default: 
                    break;
            }
            //遇到新帧、或时间戳变化或
            return new_frame;
        }

        case mp4_nal_size:
        case h264_prefix: {
            if (_have_decode_able_frame) {
                //时间戳变化了,或新的一帧，或遇到config帧，立即flush
                return frame->decodeAble() || frame->configFrame();
            }
            return false;
        }
        default: /*不可达*/ 
            assert(0); 
            return true;
    }
}

void FrameMerger::doMerge(BufferLikeString &merged, const Frame::Ptr &frame) const{
    switch (_type) {
        case none : {
            //此处是合并ps解析输出的流，解析出的流可能是半帧或多帧，不能简单的根据nal type过滤
            //此流程只用于合并ps解析输出为H264/H265，后面流程有split和忽略无效帧操作
            merged.append(frame->data(), frame->size());
            break;
        }
        case h264_prefix: {
            if (frame->prefixSize()) {
                merged.append(frame->data(), frame->size());
            } else {
                merged.append("\x00\x00\x00\x01", 4);
                merged.append(frame->data(), frame->size());
            }
            break;
        }
        case mp4_nal_size: {
            size_t nalu_size = frame->size() - frame->prefixSize();
            uint32_t network_size = htonl(nalu_size);
            merged.append((char *) &network_size, 4);
            merged.append(frame->data() + frame->prefixSize(), nalu_size);
            break;
        }
        default: /*不可达*/ assert(0); break;
    }
}

bool FrameMerger::inputFrame(const Frame::Ptr &frame, onOutput cb, BufferLikeString *buffer) {
    if (willFlush(frame)) {
        Frame::Ptr back = _frame_cache.back();
        bool have_key_frame = back->keyFrame();
        if (_frame_cache.size() != 1 || _type == mp4_nal_size || buffer) {
            //在MP4模式下，一帧数据也需要在前添加nalu_size
            Buffer::Ptr merged_frame;
            if (buffer) {
                merged_frame = std::make_shared<BufferOffset<BufferLikeString>>(*buffer);
            }
            else {
                auto tmp = std::make_shared<BufferLikeString>();
                tmp->reserve(back->size() + 1024);
                buffer = tmp.get();
                merged_frame = tmp;
            }

            _frame_cache.for_each([&](const Frame::Ptr &frame) {
                doMerge(*buffer, frame);
                if (frame->keyFrame()) {
                    have_key_frame = true;
                }
            });
            cb(back->dts(), back->pts(), merged_frame, have_key_frame);
        }
        else {
            cb(back->dts(), back->pts(), back, have_key_frame);
        }
        clear();
    }

    if (!frame) {
        return false;
    }

    if (frame->decodeAble()) {
        _have_decode_able_frame = true;
    }
    _cb = std::move(cb);
    _frame_cache.emplace_back(Frame::getCacheAbleFrame(frame));
    return true;
}

FrameMerger::FrameMerger(int type) {
    _type = type;
}

void FrameMerger::clear() {
    _frame_cache.clear();
    _have_decode_able_frame = false;
}

void FrameMerger::flush() {
    if (_cb) {
        inputFrame(nullptr, std::move(_cb), nullptr);
    }
    clear();
}
/**
 * 写帧接口转function，辅助类
 */
class FrameWriterInterfaceHelper : public FrameWriterInterface {
public:
    using Ptr = std::shared_ptr<FrameWriterInterfaceHelper>;
    using onWriteFrame = std::function<bool(const Frame::Ptr &frame)>;

    /**
     * inputFrame后触发onWriteFrame回调
     */
    FrameWriterInterfaceHelper(onWriteFrame cb) { _callback = std::move(cb); }

    virtual ~FrameWriterInterfaceHelper() = default;

    /**
     * 写入帧数据
     */
    bool inputFrame(const Frame::Ptr &frame) override { return _callback(frame); }

private:
    onWriteFrame _callback;
};

FrameWriterInterface* FrameDispatcher::addDelegate(std::function<bool(const Frame::Ptr &frame)> cb) {
    return addDelegate(std::make_shared<FrameWriterInterfaceHelper>(std::move(cb)));
}

}//namespace mediakit
