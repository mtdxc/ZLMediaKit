#ifndef ZLMEDIAKIT_RTCMEDIASOURCE_H
#define ZLMEDIAKIT_RTCMEDIASOURCE_H

#include "Rtsp/RtspMediaSourceMuxer.h"
#include "Rtsp/RtspMediaSourceImp.h"
namespace mediakit {

class RtcMediaSourceImp : public RtspMediaSourceImp {
public:
    using Ptr = std::shared_ptr<RtcMediaSourceImp>;

    RtcMediaSourceImp(const MediaTuple& tuple, int ringSize = RTP_GOP_SIZE)
        : RtspMediaSourceImp(tuple, RTC_SCHEMA, ringSize) {
    }

    RtspMediaSource::Ptr clone(const std::string &stream) override {
        auto tuple = _tuple;
        tuple.stream = stream;
        auto src_imp = std::make_shared<RtcMediaSourceImp>(tuple);
        src_imp->setSdp(getSdp());
        src_imp->setProtocolOption(getProtocolOption());
        return src_imp;
    }
};

class RtcMediaSourceMuxer : public RtspMediaSourceMuxer {
public:
    using Ptr = std::shared_ptr<RtcMediaSourceMuxer>;

    RtcMediaSourceMuxer(const MediaTuple &tuple, const ProtocolOption &option, const TitleSdp::Ptr &title = nullptr)
        : RtspMediaSourceMuxer(tuple, option, title, RTC_SCHEMA) {
    }

    bool addTrack(const Track::Ptr &track) override {
        auto t = track;
        if (_option.audio_transcode && track->getCodecId() == CodecAAC) {
            GET_CONFIG(int, bitrate, General::kOpusBitrate);
            int channels = std::dynamic_pointer_cast<AudioTrack>(track)->getAudioChannel();
            _trans = track->getTransodeTrack(CodecOpus, 48000, channels, bitrate);
            if (_trans) t = _trans;
        }
        return RtspMuxer::addTrack(t);
    }

    void onReaderChanged(MediaSource &sender, int size) override {
        RtspMediaSourceMuxer::onReaderChanged(sender, size);
        if (_trans) {
            if (size)
                _trans->addDelegate(shared_from_this());
            else
                _trans->delDelegate(this);
        }
    }
protected:
    Track::Ptr _trans;
};

}
#endif