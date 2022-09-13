#pragma once
#include "Track.h"
namespace mediakit {

class AudioSdp : public Sdp {
public:
    AudioSdp(AudioTrack* track, int payload_type = 98);

    CodecId getCodecId() const override { return _codecId; }
    std::string getSdp() const override;
protected:
    std::stringstream _printer;
    CodecId _codecId;
};

class AudioTrackImp : public AudioTrack {
public:
    typedef std::shared_ptr<AudioTrackImp> Ptr;

    /**
        * 构造函数
        * @param codecId 编码类型
        * @param sample_rate 采样率(HZ)
        * @param channels 通道数
        * @param sample_bit 采样位数，一般为16
        */
    AudioTrackImp(CodecId codecId, int sample_rate, int channels, int sample_bit) {
        _codecid = codecId;
        _sample_rate = sample_rate;
        _channels = channels;
        _sample_bit = sample_bit;
    }

    /**
        * 返回编码类型
        */
    CodecId getCodecId() const override {
        return _codecid;
    }

    /**
        * 是否已经初始化
        */
    bool ready() override {
        return true;
    }

    /**
        * 返回音频采样率
        */
    int getAudioSampleRate() const override {
        return _sample_rate;
    }

    /**
        * 返回音频采样位数，一般为16或8
        */
    int getAudioSampleBit() const override {
        return _sample_bit;
    }

    /**
        * 返回音频通道数
        */
    int getAudioChannel() const override {
        return _channels;
    }

    Sdp::Ptr getSdp() override;
    
    Track::Ptr clone() override {
        return std::make_shared<AudioTrackImp>(*this);
    }
private:
    CodecId _codecid;
    int _sample_rate;
    int _channels;
    int _sample_bit;
};

/**
 * L16音频通道
 */
class L16Track : public AudioTrackImp {
public:
    using Ptr = std::shared_ptr<L16Track>;
    L16Track(int sample_rate, int channels) : AudioTrackImp(CodecL16, sample_rate, channels, 16) {}

private:
    Track::Ptr clone() override {
        return std::make_shared<std::remove_reference<decltype(*this)>::type >(*this);
    }
};

/**
 * Opus帧音频通道
 */
class OpusTrack : public AudioTrackImp {
public:
    typedef std::shared_ptr<OpusTrack> Ptr;
    OpusTrack() : AudioTrackImp(CodecOpus, 48000, 2, 16) {}

private:
    Track::Ptr clone() override {
        return std::make_shared<std::remove_reference<decltype(*this)>::type >(*this);
    }
};

/**
 * G711音频通道
 */
class G711Track : public AudioTrackImp {
public:
    using Ptr = std::shared_ptr<G711Track>;
    G711Track(CodecId codecId, int sample_rate = 8000, int channels = 1, int sample_bit = 16) 
        : AudioTrackImp(codecId, sample_rate, channels, sample_bit) {}

private:
    Sdp::Ptr getSdp() override;

    Track::Ptr clone() override {
        return std::make_shared<std::remove_reference<decltype(*this)>::type >(*this);
    }
};
}

