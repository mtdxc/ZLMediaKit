#include "Track.h"
#include "Codec/Transcode.h"
#include "Extension/Factory.h"
#include "Poller/EventPoller.h"
#include "Util/logger.h"

namespace mediakit {

#ifdef ENABLE_FFMPEG

Track::~Track() {
    if (_parent) {
        // unregister this
        _parent->delRawDelegate(this);
    } else {
        for (auto it : _trans_tracks) {
            it.second->_parent = nullptr;
            it.second->clear();
        }
    }
}

int Track::trans_size() {
    std::unique_lock<decltype(_trans_mutex)> l(_trans_mutex);
    return _trans_tracks.size();
}

void Track::onSizeChange(size_t size) {
    if (!_parent) {
        return ;
    }
    if (size || !ready()) {
        _parent->addRawDelegate(shared_from_this());
    }
    else {
        _parent->delRawDelegate(this);
        _encoder = nullptr;
    }
}

// gop解码刷新类，管理gop缓存，并保证解码延迟最低
struct GopFlush : public std::list<Frame::Ptr> {
    GopFlush(int ms) : max_size(ms) {}
    // 关键帧标记，cacheFrame时有用
    bool key_pos = false;
    const int max_size;
    // 缓存gop帧
    void cacheFrame(const Frame::Ptr &frame) {
        auto video_key_pos = frame->keyFrame() || frame->configFrame();
        if (video_key_pos && !key_pos) {
            clear();
        }
        if (size() < max_size) {
            push_back(Frame::getCacheAbleFrame(frame));
        }
        if (!frame->dropAble()) {
            key_pos = video_key_pos;
        }
    }

    // 刷新gop帧，drop开启pts丢帧，可通过needDrop来判断是否要丢帧
    void flush(std::function<void(const Frame::Ptr &frame)> cb, bool enable_drop = true) {
        if (empty())
            return;
        flush_drop = 0;
        last_flush_pts = 0;
        if (enable_drop)
            last_flush_pts = (*rbegin())->pts();
        for (auto frame : *this) {
            cb(frame);
        }
        InfoL << "flush gop with " << size() << " items, last_pts=" << last_flush_pts;
    }

    bool needDrop(uint64_t pts) {
        if (last_flush_pts) {
            if (pts < last_flush_pts) {
                flush_drop++;
                return true;
            }
            if (flush_drop) {
                InfoL << "drop " << flush_drop << " rawFrames in gop flush";
                flush_drop = 0;
            }
            last_flush_pts = 0;
        }
        return false;
    }
    uint64_t last_flush_pts = 0;
    int flush_drop = 0;
};

bool Track::inputFrame(const Frame::Ptr &frame) {
    {
        std::unique_lock<decltype(_trans_mutex)> l(_trans_mutex);
        if (_gop && frame->getTrackType() == TrackVideo) {
            _gop->cacheFrame(frame);
        }
        if (_raw_cbs.size()) {
            if (!_decoder) {
                _decoder = std::make_shared<FFmpegDecoder>(shared_from_this());
                // 让编码器等待关键帧
                _decoder->addDecodeDrop();
                InfoL << " open decoder " << getInfo();
                _decoder->setOnDecode([this](const FFmpegFrame::Ptr &frame) { onRawFrame(frame); });
                if (_gop && _gop->size()) {
                    if (_gop->size() > _decoder->getMaxTaskSize())
                        _decoder->setMaxTaskSize(_gop->size());
                    _gop->flush([this](const Frame::Ptr &frame) { _decoder->inputFrame(frame, true, true); });
                }
            }
            _decoder->inputFrame(frame, true, true);
        } else if (_decoder) {
            InfoL << " close decoder " << getInfo();
            _decoder = nullptr;
        }
    }
    return FrameDispatcher::inputFrame(frame);
}

void Track::onRawFrame(const std::shared_ptr<AVFrame>& frame) {
    if (_gop && _gop->needDrop(frame->pts)) {
        return;
    }
    std::list<RawFrameInterface::Ptr> raw_cbs;
    {
        std::unique_lock<decltype(_trans_mutex)> l(_trans_mutex);
        for (auto it : _raw_cbs) {
            raw_cbs.push_back(it.second);
        }
    }
    for (auto it : raw_cbs) {
        it->inputRawFrame(frame);
    }
}

void Track::inputRawFrame(const std::shared_ptr<AVFrame> &frame) {
    if (!_encoder) {
        if (!_enc_cfg) {
            WarnL << "call setupEncoder first, skip frame " << frame.get();
            return;
        }
        int format = frame->format;
        int threads = 1 + (getTrackType() == TrackVideo);
        InfoL << "with threads " << threads << ", format " << format;
        _encoder = std::make_shared<FFmpegEncoder>(_enc_cfg, threads, format);
        _encoder->setOnEncode([this](const Frame::Ptr &frame) {
            frame->setIndex(getIndex());
            inputFrame(frame);
        });
    }
    if (_encoder && (size() || !ready())) {
        _encoder->inputFrame(frame, true);
    }
}

struct RawCb : public RawFrameInterface {
    using CB = std::function<void(const std::shared_ptr<AVFrame> &frame)>;
    CB _cb;
    RawCb(CB cb) : _cb(cb) {}
    // 通过 RawFrameInterface 继承
    virtual void inputRawFrame(const std::shared_ptr<AVFrame> &frame) override {
        if (_cb) _cb(frame);
    }
};
RawFrameInterface *Track::addRawDelegate(std::function<void(const std::shared_ptr<AVFrame> &frame)> cb) {
    return addRawDelegate(std::make_shared<RawCb>(cb));
}

RawFrameInterface *Track::addRawDelegate(RawFrameInterface::Ptr cb) {
    auto ret = cb.get();
    std::unique_lock<decltype(_trans_mutex)> l(_trans_mutex);
    _raw_cbs[ret] = cb;
    return ret;
}

bool Track::delRawDelegate(RawFrameInterface* cb) {
    std::unique_lock<decltype(_trans_mutex)> l(_trans_mutex);
    return _raw_cbs.erase(cb);
}

void Track::setupEncoder(int arg1, int arg2, int arg3, int bitrate) {
    Track::Ptr cfg;
    // Factory::getTrackByCodecId在未Ready时，无法获取宽高参数，
    // 因此这边采用Audio/VideoTrackImp来创建cfg
    if (getTrackType() == TrackVideo)
        cfg.reset(new VideoTrackImp(getCodecId(), arg1, arg2, arg3));
    else
        cfg.reset(new AudioTrackImp(getCodecId(), arg1, arg2, arg3));
    if (bitrate)
        cfg->setBitRate(bitrate);
    _enc_cfg = cfg;
}
#endif

Track::Ptr Track::getTransodeTrack(CodecId id, int arg1, int arg2, int bitrate) {
    Ptr ret = nullptr;
#ifdef ENABLE_FFMPEG
    if (_parent)
        return _parent->getTransodeTrack(id, arg1, arg2, bitrate);
    char key[256];
    int arg3 = (mediakit::getTrackType(id) == TrackVideo) ? 30 : 16;
    snprintf(key, sizeof(key), "%s[%dx%dx%d]", mediakit::getCodecName(id), arg1, arg2, arg3);
    if (key == getInfo()) {
        return shared_from_this();
    }
    std::unique_lock<decltype(_trans_mutex)> l(_trans_mutex);
    ret = _trans_tracks[key];
    if (!ret) {
        ret = Factory::getTrackByCodecId(id, arg1, arg2, arg3);
        if (ret) {
            static int transIdx = CodecMax;
            ret->setIndex(transIdx++);
          if (!_gop && mediakit::getTrackType(id) == TrackVideo)
              _gop = std::make_shared<GopFlush>(1000);
            ret->setupEncoder(arg1, arg2, arg3, bitrate);
            ret->_parent = this;
            ret->onSizeChange(0);
            _trans_tracks[key] = ret;
        }
    }
#endif
    return ret;
}


} // namespace mediakit
