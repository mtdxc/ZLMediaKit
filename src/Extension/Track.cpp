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

bool Track::inputFrame(const Frame::Ptr &frame) {
    bool ret = FrameDispatcher::inputFrame(frame);
    int cbSize = 0;
    {
        std::unique_lock<decltype(_trans_mutex)> l(_trans_mutex);
        cbSize = _raw_cbs.size();
    }
    if (cbSize>0) {
        if (!_decoder) {
            _decoder = std::make_shared<FFmpegDecoder>(shared_from_this());
            // 让编码器必须等待关键帧
            _decoder->addDecodeDrop();
            InfoL << " open decoder " << getInfo();
            _decoder->setOnDecode([this](const FFmpegFrame::Ptr &frame) {
                onRawFrame(frame);
            });
        }
        _decoder->inputFrame(frame, true, true);
    } else if (_decoder) {
        InfoL << " close decoder " << getInfo();
        _decoder = nullptr;
    }
    return ret;
}

void Track::onRawFrame(const std::shared_ptr<FFmpegFrame>& frame) {
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

void Track::inputRawFrame(const std::shared_ptr<FFmpegFrame> &frame) {
    if (!_encoder) {
        if (!_enc_cfg) {
            WarnL << "call setupEncoder first, skip frame " << frame->get();
            return;
        }
        int format = frame->get()->format;
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
    using CB = std::function<void(const std::shared_ptr<FFmpegFrame> &frame)>;
    CB _cb;
    RawCb(CB cb) : _cb(cb) {}
    // 通过 RawFrameInterface 继承
    virtual void inputRawFrame(const std::shared_ptr<FFmpegFrame> &frame) override {
        if (_cb) _cb(frame);
    }
};
RawFrameInterface *Track::addRawDelegate(std::function<void(const std::shared_ptr<FFmpegFrame> &frame)> cb) {
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
