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
};

}
#endif