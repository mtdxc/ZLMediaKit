#include "RtmpDemuxer.h"
#include "RtmpMediaSource.h"
#include "RtmpMediaSourceImp.h"

namespace mediakit {
RtmpMediaSource::RtmpMediaSource(const std::string& vhost, const std::string& app, const std::string& stream_id, int ring_size /*= RTMP_GOP_SIZE*/) :
    MediaSource(RTMP_SCHEMA, vhost, app, stream_id), _ring_size(ring_size)
{
}

RtmpMediaSource::~RtmpMediaSource()
{
    flush();
}

void RtmpMediaSource::getPlayerList(const std::function<void(const std::list<std::shared_ptr<void>>& info_list)>& cb, const std::function<std::shared_ptr<void>(std::shared_ptr<void>&& info)>& on_change)
{
    _ring->getInfoList(cb, on_change);
}

int RtmpMediaSource::readerCount()
{
    return _ring ? _ring->readerCount() : 0;
}

const AMFValue& RtmpMediaSource::getMetaData() const
{
    std::lock_guard<std::recursive_mutex> lock(_mtx);
    return _metadata;
}

void RtmpMediaSource::setMetaData(const AMFValue& metadata)
{
    _metadata = metadata;
    _metadata.set("server", kServerName);
    _have_video = _metadata["videocodecid"];
    _have_audio = _metadata["audiocodecid"];
    if (_ring) {
        regist();
    }
}

void RtmpMediaSource::updateMetaData(const AMFValue& metadata)
{
    std::lock_guard<std::recursive_mutex> lock(_mtx);
    _metadata = metadata;
}

void RtmpMediaSource::onWrite(RtmpPacket::Ptr pkt, bool /*= true*/)
{
    bool is_video = false;
    switch (pkt->type_id) {
    case MSG_VIDEO:
        _track_stamps[TrackVideo] = pkt->time_stamp;
        _speed[TrackVideo] += pkt->size();
        _have_video = true;
        is_video = true;
        break;
    case MSG_AUDIO:
        _track_stamps[TrackAudio] = pkt->time_stamp;
        _speed[TrackAudio] += pkt->size();
        _have_audio = true;
        break;
    default:
        break;
    }

    if (pkt->isCfgFrame()) {
        std::lock_guard<std::recursive_mutex> lock(_mtx);
        // 保存config_frame
        _config_frame_map[pkt->type_id] = pkt;
        if (!_ring) {
            //注册后收到config帧更新到各播放器
            return;
        }
    }

    if (!_ring) {
        std::weak_ptr<RtmpMediaSource> weakSelf = std::dynamic_pointer_cast<RtmpMediaSource>(shared_from_this());

        //GOP默认缓冲512组RTMP包，每组RTMP包时间戳相同(如果开启合并写了，那么每组为合并写时间内的RTMP包),
        //每次遇到关键帧第一个RTMP包，则会清空GOP缓存(因为有新的关键帧了，同样可以实现秒开)
        _ring = std::make_shared<RingType>(_ring_size, [weakSelf](int size) {
            if (auto strongSelf = weakSelf.lock())
                strongSelf->onReaderChanged(size);
            });
        if (_metadata) {
            regist();
        }
    }
    bool key = pkt->isVideoKeyFrame();
    auto stamp = pkt->time_stamp;
    PacketCache<RtmpPacket>::inputPacket(stamp, is_video, std::move(pkt), key);
}

uint32_t RtmpMediaSource::getTimeStamp(TrackType trackType)
{
    assert(trackType >= TrackInvalid && trackType < TrackMax);
    if (trackType != TrackInvalid) {
        //获取某track的时间戳
        return _track_stamps[trackType];
    }

    //获取所有track的最小时间戳
    uint32_t ret = UINT32_MAX;
    for (auto& stamp : _track_stamps) {
        if (stamp > 0 && stamp < ret) {
            ret = stamp;
        }
    }
    return ret;
}

void RtmpMediaSource::clearCache()
{
    PacketCache<RtmpPacket>::clearCache();
    _ring->clearCache();
}

void RtmpMediaSource::onFlush(std::shared_ptr<std::list<RtmpPacket::Ptr> > rtmp_list, bool key_pos)
{
    //如果不存在视频，那么就没有存在GOP缓存的意义，所以is_key一直为true确保一直清空GOP缓存
    _ring->write(std::move(rtmp_list), _have_video ? key_pos : true);
}



RtmpMediaSourceImp::RtmpMediaSourceImp(const std::string& vhost, const std::string& app, const std::string& id, int ringSize /*= RTMP_GOP_SIZE*/) : RtmpMediaSource(vhost, app, id, ringSize)
{
    _demuxer = std::make_shared<RtmpDemuxer>();
    _demuxer->setTrackListener(this);
}

void RtmpMediaSourceImp::setMetaData(const AMFValue& metadata)
{
    if (!_demuxer->loadMetaData(metadata)) {
        //该metadata无效，需要重新生成
        _metadata = metadata;
        _recreate_metadata = true;
    }
    RtmpMediaSource::setMetaData(metadata);
}

void RtmpMediaSourceImp::onWrite(RtmpPacket::Ptr pkt, bool /*= true*/)
{
    if (!_all_track_ready || _muxer->isEnabled()) {
        //未获取到所有Track后，或者开启转协议，那么需要解复用rtmp
        _demuxer->inputRtmp(pkt);
    }
    RtmpMediaSource::onWrite(std::move(pkt));
}

int RtmpMediaSourceImp::totalReaderCount()
{
    return readerCount() + (_muxer ? _muxer->totalReaderCount() : 0);
}

void RtmpMediaSourceImp::setProtocolOption(const ProtocolOption& option)
{
    //不重复生成rtmp
    _option = option;
    //不重复生成rtmp协议
    _option.enable_rtmp = false;
    _muxer = std::make_shared<MultiMediaSourceMuxer>(getVhost(), getApp(), getId(), _demuxer->getDuration(), _option);
    _muxer->setMediaListener(getListener());
    _muxer->setTrackListener(std::static_pointer_cast<RtmpMediaSourceImp>(shared_from_this()));
    //让_muxer对象拦截一部分事件(比如说录像相关事件)
    MediaSource::setListener(_muxer);

    for (auto& track : _demuxer->getTracks(false)) {
        _muxer->addTrack(track);
        track->addDelegate(_muxer);
    }
}

const ProtocolOption& RtmpMediaSourceImp::getProtocolOption() const
{
    return _option;
}

bool RtmpMediaSourceImp::addTrack(const Track::Ptr& track)
{
    if (_muxer) {
        if (_muxer->addTrack(track)) {
            track->addDelegate(_muxer);
            return true;
        }
    }
    return false;
}

void RtmpMediaSourceImp::addTrackCompleted()
{
    if (_muxer) {
        _muxer->addTrackCompleted();
    }
}

void RtmpMediaSourceImp::resetTracks()
{
    if (_muxer) {
        _muxer->resetTracks();
    }
}

void RtmpMediaSourceImp::onAllTrackReady()
{
    _all_track_ready = true;

    if (_recreate_metadata) {
        //更新metadata
        for (auto& track : _muxer->getTracks()) {
            Metadata::addTrack(_metadata, track);
        }
        RtmpMediaSource::updateMetaData(_metadata);
    }
}

void RtmpMediaSourceImp::setListener(const std::weak_ptr<MediaSourceEvent>& listener)
{
    if (_muxer) {
        //_muxer对象不能处理的事件再给listener处理
        _muxer->setMediaListener(listener);
    }
    else {
        //未创建_muxer对象，事件全部给listener处理
        MediaSource::setListener(listener);
    }
}
}

