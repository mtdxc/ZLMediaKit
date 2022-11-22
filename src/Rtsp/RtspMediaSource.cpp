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
    * ����rtp
    * @param rtp rtp��
    * @param keyPos �ð��Ƿ�Ϊ�ؼ�֡�ĵ�һ����
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
        //GOPĬ�ϻ���512��RTP����ÿ��RTP��ʱ�����ͬ(��������ϲ�д�ˣ���ôÿ��Ϊ�ϲ�дʱ���ڵ�RTP��),
        //ÿ�������ؼ�֡��һ��RTP����������GOP����(��Ϊ���µĹؼ�֡�ˣ�ͬ������ʵ���뿪)
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
        //��ȡ������Track�󣬲���δ����תЭ�飬��ô����Ҫ�⸴��rtp
        //�ڹر�rtp�⸴�ú��޷�֪���Ƿ�Ϊ�ؼ�֡�������ᵼ���޷��뿪�����߿�������
        key_pos = rtp->type == TrackVideo;
    }
    else {
        //��Ҫ�⸴��rtp
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
    //��_muxer��������һ�����¼�(����˵¼������¼�)
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
        //_muxer�����ܴ�����¼��ٸ�listener����
        _muxer->setMediaListener(listener);
    }
    else {
        //δ����_muxer�����¼�ȫ����listener����
        MediaSource::setListener(listener);
    }
}

}


