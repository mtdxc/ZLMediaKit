#pragma once
#include <memory>
#include "Common/MediaSink.h"
#include "toolkit.h"

namespace mediakit {
class MP4Muxer;
class RtmpMuxer;
class FlvRecorder;
class MediaPlayer;
class Stamp;
// 支持将流录制成mp4和flv
class RecordSink : public MediaSink {
    std::shared_ptr<MP4Muxer> mp4_;
    std::shared_ptr<RtmpMuxer> rtmp_;
    std::shared_ptr<FlvRecorder> flv_;
    Stamp* _stamp;
    std::string path_;
public:
    RecordSink(const std::string& path);
    ~RecordSink();
    /**
    * 某track已经准备好，其ready()状态返回true，
    * 此时代表可以获取其例如sps pps等相关信息了
    * @param track
    */
    bool onTrackReady(const Track::Ptr & track) override;

    /**
        * 所有Track已经准备好，
        */
    virtual void onAllTrackReady() override;

    /**
    * 某Track输出frame，在onAllTrackReady触发后才会调用此方法
    * @param frame
    */
    bool onTrackFrame(const Frame::Ptr &frame_in) override;
    float duration() const;
};

// nvr 拉流
class NvrRecord : public FrameWriterInterface,
    public std::enable_shared_from_this<NvrRecord> {
public:
    using onSnap = std::function<void(bool success, const std::string &err_msg)>;
    // 将url的文件存放到path中，最大长度为maxSec
	NvrRecord(std::string url, std::string path, int maxSec, onSnap cb);
    ~NvrRecord();

    void Stop();
    void Connect();
    bool inputFrame(const Frame::Ptr &frame) override;
    float duration() const {
        return writer_ ? writer_->duration() : 0;
    }
	
    void addCbs(onSnap cb) { cbs_.push_back(cb); }
    void FireCbs(bool val, std::string path) {
        for (auto cb : cbs_) {
            cb(val, path);
        }
        cbs_.clear();
    }
	int getIdentifier() const { return id_; }
protected:
	int id_;
    std::list<onSnap> cbs_;

    void Reconnect();
    void onFinish();
    void onConnect();

    std::string path_, url_;
    int max_sec_ = 0;
    std::shared_ptr<RecordSink> writer_;
    std::shared_ptr<MediaPlayer> player_;
    std::shared_ptr<toolkit::Timer> timer_;
    onSnap cb_;
};

}


