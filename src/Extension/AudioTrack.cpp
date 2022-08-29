#include "AudioTrack.h"

namespace mediakit {
AudioSdp::AudioSdp(AudioTrack * track, int payload_type) :Sdp(track->getAudioSampleRate(), payload_type) {
    _codecId = track->getCodecId();
    int bitrate = track->getBitRate() / 1024;
    _printer << "m=audio 0 RTP/AVP " << payload_type << "\r\n";
    if (bitrate) {
        _printer << "b=AS:" << bitrate << "\r\n";
    }
    _printer << "a=rtpmap:" << payload_type << " " << getCodecName() << "/" << track->getAudioSampleRate() << "/" << track->getAudioChannel() << "\r\n";
    //_printer << "a=control:trackID=" << (int)TrackAudio << "\r\n";
}

std::string AudioSdp::getSdp() const {
    return _printer.str() + "a=control:trackID=1\r\n";
}

Sdp::Ptr mediakit::G711Track::getSdp()
{
    if (!ready()) {
        WarnL << getCodecName() << " TrackÎ´×¼±¸ºÃ";
        return nullptr;
    }

    auto payload_type = 98;
    if (getAudioSampleRate() == 8000 && getAudioChannel() == 1) {
        // https://datatracker.ietf.org/doc/html/rfc3551#section-6
        payload_type = (getCodecId() == CodecG711U) ? Rtsp::PT_PCMU : Rtsp::PT_PCMA;
    }

    return std::make_shared<AudioSdp>(this, payload_type);
}
}
