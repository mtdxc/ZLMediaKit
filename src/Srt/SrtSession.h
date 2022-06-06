#include "Network/Session.h"
#include "Common/MultiMediaSourceMuxer.h"
#include "Rtp/Decoder.h"
#include "TS/TsMediaSource.h"
namespace mediakit {

class SrtSession : public toolkit::Session
    , public MediaSinkInterface
    , public MediaSourceEvent {
    uint64_t  _total_bytes = 0;
    toolkit::Ticker _alive_ticker;

    MediaInfo _media_info;
    bool _is_pusher = false;
    // for player 
    TSMediaSource::RingType::RingReader::Ptr _ts_reader;
    // for pusher 
    MultiMediaSourceMuxer::Ptr _muxer;
    DecoderImp::Ptr _decoder;
    std::recursive_mutex _func_mtx;
    std::deque<std::function<void()> > _cached_func;
public:
    ~SrtSession();
    bool isPusher() {
        return _is_pusher;
    }

    ///////MediaSourceEvent override///////
    // 关闭
    bool close(MediaSource &sender, bool force) override;
    // 播放总人数
    int totalReaderCount(MediaSource &sender) override;
    // 获取媒体源类型
    MediaOriginType getOriginType(MediaSource &sender) const override;
    // 获取媒体源url或者文件路径
    std::string getOriginUrl(MediaSource &sender) const override;
    // 获取媒体源客户端相关信息
    std::shared_ptr<SockInfo> getOriginSock(MediaSource &sender) const override;

    // MediaSinkInterface override
    bool inputFrame(const Frame::Ptr &frame) override;
    bool addTrack(const Track::Ptr & track) override;
    void addTrackCompleted() override;
    void resetTracks() override {};

private:
    void emitOnPublish();
    void emitOnPlay();

    void doPlay();
    void doCachedFunc();

public:
    // toolkit::Session override
    virtual void attachServer(const toolkit::Server &server) override;
    virtual std::string getIdentifier() const override;

    virtual void onRecv(const toolkit::Buffer::Ptr &buf) override;
    virtual void onError(const toolkit::SockException &err) override;
    virtual void onManager() override;
};
}
