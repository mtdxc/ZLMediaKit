#include "RtspMediaSourceImp.h"
#include "RtspDemuxer.h"

namespace mediakit {

void RtspMediaSource::setSdp(const std::string& sdp) {
    SdpParser sdp_parser(sdp);
    _tracks[TrackVideo] = sdp_parser.getTrack(TrackVideo);
    _tracks[TrackAudio] = sdp_parser.getTrack(TrackAudio);
    _have_video = (bool)_tracks[TrackVideo];
    _sdp = sdp_parser.toString();
    if (_ring) {
        regist();
    }
}

/**
    * 输入rtp
    * @param rtp rtp包
    * @param keyPos 该包是否为关键帧的第一个包
    */
void RtspMediaSource::onWrite(RtpPacket::Ptr rtp, bool keyPos) {
    _speed[rtp->type] += rtp->size();
    assert(rtp->type >= 0 && rtp->type < TrackMax);
    auto& track = _tracks[rtp->type];
    if (track) {
        track->_seq = rtp->getSeq();
        track->_time_stamp = rtp->getStampMS(false);
        track->_ssrc = rtp->getSSRC();
    }
    if (!_ring) {
        std::weak_ptr<RtspMediaSource> weakSelf = std::dynamic_pointer_cast<RtspMediaSource>(shared_from_this());
        //GOP默认缓冲512组RTP包，每组RTP包时间戳相同(如果开启合并写了，那么每组为合并写时间内的RTP包),
        //每次遇到关键帧第一个RTP包，则会清空GOP缓存(因为有新的关键帧了，同样可以实现秒开)
        _ring = std::make_shared<RingType>(_ring_size, [weakSelf](int size) {
            if (auto strongSelf = weakSelf.lock())
                strongSelf->onReaderChanged(size);
            });
        if (!_sdp.empty()) {
            regist();
        }
    }
    auto stamp = rtp->getStampMS(true);
    bool is_video = rtp->type == TrackVideo;
    PacketCache<RtpPacket>::inputPacket(stamp, is_video, std::move(rtp), keyPos);
}

RtspMediaSourceImp::RtspMediaSourceImp(const std::string& vhost, const std::string& app, const std::string& id, const std::string& schema, int ringSize) 
    : RtspMediaSource(vhost, app, id, schema, ringSize)
{
    _demuxer = std::make_shared<RtspDemuxer>();
    _demuxer->setTrackListener(this);
}

void RtspMediaSourceImp::setSdp(const std::string& strSdp)
{
    if (!getSdp().empty()) {
        return;
    }
    _demuxer->loadSdp(strSdp);
    RtspMediaSource::setSdp(strSdp);
}

void RtspMediaSourceImp::onWrite(RtpPacket::Ptr rtp, bool key_pos)
{
    if (_all_track_ready && !_muxer->isEnabled()) {
        //获取到所有Track后，并且未开启转协议，那么不需要解复用rtp
        //在关闭rtp解复用后，无法知道是否为关键帧，这样会导致无法秒开，或者开播花屏
        key_pos = rtp->type == TrackVideo;
    }
    else {
        //需要解复用rtp
        key_pos = _demuxer->inputRtp(rtp);
    }
    RtspMediaSource::onWrite(std::move(rtp), key_pos);
}

int RtspMediaSourceImp::totalReaderCount()
{
    return readerCount() + (_muxer ? _muxer->totalReaderCount() : 0);
}

void RtspMediaSourceImp::setProtocolOption(const ProtocolOption& option)
{
    _option = option;
    _option.enable_rtc = this->getSchema() != RTC_SCHEMA;
    _option.enable_rtsp = this->getSchema() != RTSP_SCHEMA;
    _muxer = std::make_shared<MultiMediaSourceMuxer>(getVhost(), getApp(), getId(), _demuxer->getDuration(), _option);
    _muxer->setMediaListener(getListener());
    _muxer->setTrackListener(std::static_pointer_cast<RtspMediaSourceImp>(shared_from_this()));
    //让_muxer对象拦截一部分事件(比如说录像相关事件)
    MediaSource::setListener(_muxer);

    for (auto& track : _demuxer->getTracks(false)) {
        this->addTrack(track);
    }
}

bool RtspMediaSourceImp::addTrack(const Track::Ptr& track)
{
    if (_muxer) {
        if (_muxer->addTrack(track)) {
            track->addDelegate(_muxer);
            return true;
        }
    }
    return false;
}

void RtspMediaSourceImp::addTrackCompleted()
{
    if (_muxer) {
        _muxer->addTrackCompleted();
    }
}

void RtspMediaSourceImp::resetTracks()
{
    if (_muxer) {
        _muxer->resetTracks();
    }
}

void RtspMediaSourceImp::setListener(const std::weak_ptr<MediaSourceEvent>& listener)
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


