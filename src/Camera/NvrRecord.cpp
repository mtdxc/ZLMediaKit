#include "NvrRecord.h"
#include <atomic>
#include "Record/MP4Muxer.h"
#include "Rtmp/FlvMuxer.h"
#include "Rtmp/RtmpMuxer.h"
#include "Player/MediaPlayer.h"
#include "Util/util.h"
#include "Common/Stamp.h"
#include "../../server/FFmpegSource.h"
#include "Common/config.h"

using namespace toolkit;

namespace mediakit {
static std::atomic<int> sRecID(0);
RecordSink::RecordSink(const std::string& path) : path_(path)
{
    _stamp = new Stamp[2];
    if (strstr(path.c_str(), ".mp4")) {
        mp4_.reset(new MP4Muxer);
        mp4_->openMP4(path);
    }
    else {//".flv"
        rtmp_ = std::make_shared<RtmpMuxer>(nullptr);
    }
}

RecordSink::~RecordSink() {
    delete [] _stamp;
}

bool RecordSink::onTrackReady(const Track::Ptr & track)
{
    bool ret = false;
    if (rtmp_ && rtmp_->addTrack(track))
        ret = true;
    if (mp4_ && mp4_->addTrack(track))
        ret = true;
    return ret;
}

void RecordSink::onAllTrackReady()
{
    if (mp4_)
        mp4_->addTrackCompleted();
    if (rtmp_) {
        rtmp_->addTrackCompleted();
        flv_ = std::make_shared<FlvRecorder>();
        flv_->startRecord(rtmp_.get(), path_);
    }
}

bool RecordSink::onTrackFrame(const Frame::Ptr &frame_in)
{
    bool ret = false;
    auto frame = frame_in;
    frame = std::make_shared<FrameStamp>(frame, _stamp[frame->getTrackType()], false);
    if (mp4_) {
        ret = mp4_->inputFrame(frame);
    }
    if (rtmp_) {
        ret = rtmp_->inputFrame(frame);
    }
    return ret;
}

float RecordSink::duration() const
{
    int64_t tsp = _stamp[0].getRelativeStamp();
    if (tsp < _stamp[1].getRelativeStamp())
        tsp = _stamp[1].getRelativeStamp();
    return tsp / 1000.0f;
}


// 将url的文件存放到path中，最大长度为maxSec
NvrRecord::NvrRecord(std::string url, std::string path, int maxSec, onSnap cb) {
	id_ = ++sRecID;
	InfoT << "construct with url " << url << ", path " << path << ", maxSec=" << maxSec;
	max_sec_ = maxSec;
	path_ = path;
	url_ = url;
	cb_ = cb;
}

NvrRecord::~NvrRecord()
{
	InfoT;
	Stop();
}

void NvrRecord::Reconnect()
{
    writer_ = nullptr;
    player_ = nullptr;
    InfoT << "schedule reconnect after 60s";
    std::weak_ptr<NvrRecord> weakSelf = shared_from_this();
    timer_ = std::make_shared<Timer>(60, [weakSelf]() {
        if (auto strong_self = weakSelf.lock()) {
            strong_self->Connect();
        }
        return false;
    }, nullptr);
}

void NvrRecord::onConnect() {
    // 尝试倍数收流
    // player_->speed(2);
    // 取消重试定时器
    timer_.reset();
    if (!player_) return;
	InfoT << "connected, create writer";
    auto pThis = shared_from_this();
    writer_ = std::make_shared<RecordSink>(path_);
    for (auto track : player_->getTracks(false)) {
        writer_->addTrack(track);
        track->addDelegate(pThis);
    }
    writer_->addTrackCompleted();
}

void NvrRecord::Connect()
{
    std::weak_ptr<NvrRecord> weakThis = shared_from_this();
#if 0
    GET_CONFIG(bool, useFfmpeg, "camera.useFfmpeg");
    if (useFfmpeg) {
        FFmpegSnap::makeRecord(url_, path_, max_sec_, [weakThis](bool success,const std::string& msg) {
            auto pThis = weakThis.lock();
            if (!pThis) {
                return;
            }
            if (success) {
                pThis->onFinish();
            }
            else {
                WarnL << "NvrRecord ffmpeg error:" << msg;
                pThis->Reconnect();
            }
        });
        return;
    }
#endif
    writer_ = nullptr;
    player_ = std::make_shared<MediaPlayer>();
    player_->setOnPlayResult([weakThis](const SockException &ex) {
        auto pThis = weakThis.lock();
        if (!pThis) {
            return;
        }

        if (ex) {
            WarnL << pThis->getIdentifier() << " connect error:" <<  ex.what();
            pThis->Reconnect();
        }
        else {
            pThis->onConnect();
        }
    });

    player_->setOnShutdown([weakThis](const SockException &ex) {
        auto pThis = weakThis.lock();
        if (!pThis) {
            return;
        }
		WarnL << pThis->getIdentifier() << " shutdown: " << ex.what();
		if (pThis->duration() > 0) {
            pThis->onFinish();
        }
        else
            pThis->Reconnect();
    });
    (*player_)[Client::kTimeoutMS] = max_sec_ * 1500;
    player_->play(url_);
    InfoT << url_;
}

void NvrRecord::Stop()
{
    if (player_) {
		auto p = std::move(player_);
		// stop可能在inputFrame中调用，这边异步调用
		p->getPoller()->async([p]() {}, false);
    }
    if (writer_) {
        writer_ = nullptr;
    }
    if (timer_) {
        timer_.reset();
    }
}

bool NvrRecord::inputFrame(const Frame::Ptr &frame)
{
    auto writer = writer_;
    if (!writer)
        return false;
    if (max_sec_ > 0) {
        if (writer->duration() >= max_sec_) {
            // 避免文件无法重命名
            writer = nullptr;
            onFinish();
            return false;
        }
    }
    return writer->onTrackFrame(frame);
}

void NvrRecord::onFinish()
{
    InfoT << "finish with duration " << duration() << ", path " << path_;
    Stop();
    if (cb_)
        cb_(true, path_);
}

}

