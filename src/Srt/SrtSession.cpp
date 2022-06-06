#include "SrtSession.h"
#include "Common/Parser.h"
#include "srt/srt.h"
#include "srt/udt.h"
using namespace toolkit;
using namespace mediakit;

SrtSession::~SrtSession()
{
    InfoP(this);
    uint64_t duration = _alive_ticker.createdTime() / 1000;
    WarnP(this) << (_is_pusher ? "srt 推流器(" : "srt 播放器(")
        << _media_info._vhost << "/"
        << _media_info._app << "/"
        << _media_info._streamid
        << ")断开,耗时(s):" << duration;

    //流量统计事件广播
    GET_CONFIG(uint32_t, iFlowThreshold, General::kFlowThreshold);
    if (_total_bytes >= iFlowThreshold * 1024) {
        NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastFlowReport, _media_info, _total_bytes, duration, false, static_cast<SockInfo &>(*this));
    }
}

std::string SrtSession::getIdentifier() const
{
    return _media_info._streamid;
}

void SrtSession::onRecv(const Buffer::Ptr &buf)
{
    _alive_ticker.resetTime();
    _total_bytes += buf->size();
    if (!_is_pusher) {
        WarnP(this) << "ignore player data";
        return;
    }
    if (_decoder) {
        _decoder->input(reinterpret_cast<const uint8_t *>(buf->data()), buf->size());
    }
    else {
        WarnP(this) << " not reach this";
    }
}

void SrtSession::onError(const SockException &err)
{
    WarnP(this) << err.what();
}

void SrtSession::onManager()
{
    GET_CONFIG(uint32_t, keepAliveSec, Http::kKeepAliveSecond);
    if (_alive_ticker.elapsedTime() > keepAliveSec * 1000) {
        //1分钟超时
        shutdown(SockException(Err_timeout, "session timeout"));
    }
}

void SrtSession::attachServer(const toolkit::Server &server)
{
    std::string streamid = UDT::getstreamid(this->getSock()->rawFD());
    _media_info.parse("srt://" + streamid);

    auto params = Parser::parseArgs(_media_info._param_strs);
    if (params["type"] == "push") {
        _is_pusher = true;
        _decoder = DecoderImp::createDecoder(DecoderImp::decoder_ts, this);
        emitOnPublish();
    }
    else {
        _is_pusher = false;
        emitOnPlay();
    }
}

void SrtSession::emitOnPlay() {
    std::weak_ptr<SrtSession> weak_self = std::static_pointer_cast<SrtSession>(shared_from_this());
    Broadcast::AuthInvoker invoker = [weak_self](const std::string &err) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        strong_self->getPoller()->async([strong_self, err] {
            if (err != "") {
                strong_self->shutdown(SockException(Err_refused, err));
            }
            else {
                strong_self->doPlay();
            }
        });
    };

    auto flag = NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastMediaPlayed, _media_info, invoker, *this);
    if (!flag) {
        doPlay();
    }
}

void SrtSession::doPlay() {
    //异步查找直播流
    std::weak_ptr<SrtSession> weak_self = std::static_pointer_cast<SrtSession>(shared_from_this());
    MediaInfo info = _media_info;
    info._schema = TS_SCHEMA;
    MediaSource::findAsync(info, shared_from_this(), [weak_self](const MediaSource::Ptr &src) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            //本对象已经销毁
            TraceL << "本对象已经销毁";
            return;
        }
        if (!src) {
            //未找到该流
            TraceL << "未找到该流";
            strong_self->shutdown(SockException(Err_shutdown));
        }
        else {
            TraceL << "找到该流";
            auto ts_src = std::dynamic_pointer_cast<TSMediaSource>(src);
            assert(ts_src);
            ts_src->pause(false);
            strong_self->_ts_reader = ts_src->getRing()->attach(strong_self->getPoller());
            strong_self->_ts_reader->setDetachCB([weak_self]() {
                auto strong_self = weak_self.lock();
                if (strong_self) {
                    strong_self->shutdown(SockException(Err_shutdown));
                }
            });

            strong_self->_ts_reader->setReadCB([weak_self](const TSMediaSource::RingDataType &ts_list) {
                auto strong_self = weak_self.lock();
                if (!strong_self) {
                    //本对象已经销毁
                    return;
                }
                size_t i = 0;
                auto size = ts_list->size();
                ts_list->for_each([&](const TSPacket::Ptr &ts) {
                    //strong_self->onSendTSData(ts, ++i == size); 
                    strong_self->send(ts);
                });
            });
        };
    });
}


void SrtSession::emitOnPublish() {
    std::weak_ptr<SrtSession> weak_self = std::static_pointer_cast<SrtSession>(shared_from_this());
    Broadcast::PublishAuthInvoker invoker = [weak_self](const std::string &err, const ProtocolOption &option) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        if (err.empty()) {
            strong_self->_muxer = std::make_shared<MultiMediaSourceMuxer>(strong_self->_media_info._vhost,
                strong_self->_media_info._app,
                strong_self->_media_info._streamid, 0.0f,
                option);
            strong_self->_muxer->setMediaListener(strong_self);
            strong_self->doCachedFunc();
            InfoP(strong_self) << "允许 srt 推流";
        }
        else {
            WarnP(strong_self) << "禁止 srt 推流:" << err;
            strong_self->shutdown(SockException(Err_refused, err));
        }
    };

    //触发推流鉴权事件
    auto flag = NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastMediaPublish, MediaOriginType::srt_push, _media_info, invoker, *this);
    if (!flag) {
        //该事件无人监听,默认不鉴权
        invoker("", ProtocolOption());
    }
}


bool SrtSession::close(MediaSource &sender, bool force) {
    if (!force && totalReaderCount(sender)) {
        return false;
    }
    std::string err = StrPrinter << "close media:" << sender.getUrl() << " " << force;
    std::weak_ptr<SrtSession> weak_self = std::static_pointer_cast<SrtSession>(shared_from_this());
    getPoller()->async([weak_self, err]() {
        auto strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->shutdown(SockException(Err_shutdown, err));
            //主动关闭推流，那么不延时注销
            strong_self->_muxer = nullptr;
        }
    });
    return true;
}

int SrtSession::totalReaderCount(MediaSource &sender) {
    return _muxer ? _muxer->totalReaderCount() : sender.readerCount();
}

MediaOriginType SrtSession::getOriginType(MediaSource &sender) const {
    return MediaOriginType::srt_push;
}

std::string SrtSession::getOriginUrl(MediaSource &sender) const {
    return _media_info._full_url;
}

std::shared_ptr<SockInfo> SrtSession::getOriginSock(MediaSource &sender) const {
    auto ret = std::static_pointer_cast<const toolkit::SockInfo>(shared_from_this());
    return std::const_pointer_cast<SockInfo>(ret);
}


bool SrtSession::inputFrame(const Frame::Ptr &frame) {
    if (_muxer) {
        return _muxer->inputFrame(frame);
    }
    if (_cached_func.size() > 200) {
        WarnL << "cached frame of track(" << frame->getCodecName() << ") is too much, now dropped";
        return false;
    }
    auto frame_cached = Frame::getCacheAbleFrame(frame);
    std::lock_guard<std::recursive_mutex> lck(_func_mtx);
    _cached_func.emplace_back([this, frame_cached]() {
        _muxer->inputFrame(frame_cached);
        });
    return true;
}

bool SrtSession::addTrack(const Track::Ptr &track) {
    if (_muxer) {
        return _muxer->addTrack(track);
    }

    std::lock_guard<std::recursive_mutex> lck(_func_mtx);
    _cached_func.emplace_back([this, track]() {
        _muxer->addTrack(track);
        });
    return true;
}

void SrtSession::addTrackCompleted() {
    if (_muxer) {
        _muxer->addTrackCompleted();
    }
    else {
        std::lock_guard<std::recursive_mutex> lck(_func_mtx);
        _cached_func.emplace_back([this]() {
            _muxer->addTrackCompleted();
        });
    }
}

void SrtSession::doCachedFunc() {
    std::lock_guard<std::recursive_mutex> lck(_func_mtx);
    for (auto &func : _cached_func) {
        func();
    }
    _cached_func.clear();
}
