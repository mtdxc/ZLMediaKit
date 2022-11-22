﻿#ifndef ZLMEDIAKIT_SRT_TRANSPORT_IMP_H
#define ZLMEDIAKIT_SRT_TRANSPORT_IMP_H
#include "Common/Stamp.h"
#include "Common/MultiMediaSourceMuxer.h"
#include "TS/Decoder.h"
#include "SrtTransport.hpp"
#include "TS/TSMediaSource.h"
#include <deque>
#include <mutex>

namespace SRT {
using namespace mediakit;
class SrtTransportImp
    : public SrtTransport
    //, public toolkit::SockInfo
    , public MediaSinkInterface
    , public MediaSourceEvent {
public:
    SrtTransportImp(const toolkit::EventPollerPtr &poller);
    ~SrtTransportImp();

    void inputSockData(uint8_t *buf, int len, struct sockaddr_storage *addr) override {
        SrtTransport::inputSockData(buf, len, addr);
        _total_bytes += len;
    }
    void onSendTSData(const toolkit::Buffer::Ptr &buffer, bool flush) override { SrtTransport::onSendTSData(buffer, flush); }
    /* SockInfo override
    std::string get_local_ip() override;
    uint16_t get_local_port() override;
    std::string get_peer_ip() override;
    uint16_t get_peer_port() override;
    */
    std::string getIdentifier() const;
protected:
    ///////SrtTransport override///////
    int getLatencyMul() override;
    int getPktBufSize() override;
    float getTimeOutSec() override;
    void onSRTData(DataPacket::Ptr pkt) override;
    void onShutdown(const toolkit::SockException &ex) override;
    void onHandShakeFinished(std::string &streamid, struct sockaddr_storage *addr) override;

    void sendPacket(toolkit::Buffer::Ptr pkt, bool flush = true) override {
        _total_bytes += pkt->size();
        SrtTransport::sendPacket(pkt, flush);
    }

    bool isPusher() override { return _is_pusher; }

    ///////MediaSourceEvent override///////
    // 关闭
    bool close(MediaSource &sender) override;
    // 获取媒体源类型
    MediaOriginType getOriginType(MediaSource &sender) const override;
    // 获取媒体源url或者文件路径
    std::string getOriginUrl(MediaSource &sender) const override;
    // 获取媒体源客户端相关信息
    std::shared_ptr<toolkit::SockInfo> getOriginSock(MediaSource &sender) const override;

    ///////MediaSinkInterface override///////
    void resetTracks() override {};
    void addTrackCompleted() override;
    bool addTrack(const Track::Ptr &track) override;
    bool inputFrame(const Frame::Ptr &frame) override;

private:
    bool parseStreamid(std::string &streamid);
    void emitOnPublish();
    void emitOnPlay();

    void doPlay();
    void doCachedFunc();

private:
    bool _is_pusher = true;
    MediaInfo _media_info;
    uint64_t _total_bytes = 0;
    toolkit::Ticker _alive_ticker;
    std::unique_ptr<sockaddr_storage> _addr;
    // for player
    TSMediaSource::RingType::RingReader::Ptr _ts_reader;
    // for pusher
    MultiMediaSourceMuxer::Ptr _muxer;
    DecoderImp::Ptr _decoder;
    std::recursive_mutex _func_mtx;
    std::deque<std::function<void()>> _cached_func;

    std::unordered_map<int, Stamp> _type_to_stamp;
};

} // namespace SRT

#endif // ZLMEDIAKIT_SRT_TRANSPORT_IMP_H
