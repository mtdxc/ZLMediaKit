/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "WebRtcServer.h"
#include "WebRtcTransport.h"
#include "RtcMediaSource.h"
#include <iostream>
#include "RtpExt.h"
#include "Rtcp/Rtcp.h"
#include "Rtcp/RtcpFCI.h"
#include "Rtsp/RtpReceiver.h"
#include "Common/config.h"
#include "WebRtcEchoTest.h"
#include "WebRtcPlayer.h"
#include "WebRtcPusher.h"

#define RTP_SSRC_OFFSET 1
#define RTX_SSRC_OFFSET 2
#define RTP_CNAME "zlmediakit-rtp"
#define RTP_LABEL "zlmediakit-label"
#define RTP_MSLABEL "zlmediakit-mslabel"
#define RTP_MSID RTP_MSLABEL " " RTP_LABEL

using std::string;
using namespace toolkit;
namespace mediakit {

// RTC配置项目
namespace Rtc {
#define RTC_FIELD "rtc."
// rtp和rtcp接受超时时间
const string kTimeOutSec = RTC_FIELD "timeoutSec";
// 服务器外网ip
const string kExternIP = RTC_FIELD "externIP";
// 设置remb比特率，非0时关闭twcc并开启remb。该设置在rtc推流时有效，可以控制推流画质
const string kRembBitRate = RTC_FIELD "rembBitRate";
// webrtc单端口udp服务器
const string kPort = RTC_FIELD "port";
const string kTcpPort = RTC_FIELD "tcpPort";

// 设置aac转码比特率
const string kAacBitRate = RTC_FIELD"aacBitRate";
// 设置opus转码比特率
const string kOpusBitRate = RTC_FIELD"opusBitRate";
const string kDumpRtcp =  RTC_FIELD"dumpRtcp";
const string kDumpRtp1 =  RTC_FIELD"dumpRtp1";
const string kDumpRtp2 =  RTC_FIELD"dumpRtp2";
const string kDumpNack =  RTC_FIELD"dumpNack";
static onceToken token([]() {
    mINI::Instance()[kTimeOutSec] = 15;
    mINI::Instance()[kExternIP] = "";
    mINI::Instance()[kRembBitRate] = 0;
    mINI::Instance()[kPort] = 8000;
    mINI::Instance()[kTcpPort] = 8000;
    mINI::Instance()[kAacBitRate] = 64000;
    mINI::Instance()[kOpusBitRate] = 64000;

    mINI::Instance()[kDumpRtcp] = false;
    mINI::Instance()[kDumpRtp1] = false;
    mINI::Instance()[kDumpRtp2] = false;
    mINI::Instance()[kDumpNack] = false;
});

}//namespace RTC

GET_CONFIG(bool, dumpRtcp, Rtc::kDumpRtcp);
GET_CONFIG(bool, dumpRtp1, Rtc::kDumpRtp1);
GET_CONFIG(bool, dumpRtp2, Rtc::kDumpRtp2);
GET_CONFIG(bool, dumpNack, Rtc::kDumpNack);

std::string getPeerAddress(RTC::TransportTuple* tuple) {
    char str[64];
    uint16_t port;
    switch (tuple->sa_family) {
    case AF_INET:
        if (auto addr = (sockaddr_in*)tuple) {
            inet_ntop(AF_INET, &addr->sin_addr, str, sizeof(str));
            port = htons(addr->sin_port);
        }
        break;
    case AF_INET6:
        if (auto addr = (sockaddr_in6*)tuple) {
            inet_ntop(AF_INET6, &addr->sin6_addr, str, sizeof(str));
            port = htons(addr->sin6_port);
        }
        break;
    default:
        return "";
    }
    return StrPrinter << str << ":" << port;
}

static void translateIPFromEnv(std::vector<std::string> &v) {
    for (auto iter = v.begin(); iter != v.end();) {
        if (start_with(*iter, "$")) {
            std::string ip = getenv(iter->c_str() + 1);
            if (ip.empty()) {
                iter = v.erase(iter);
            } else {
                *iter++ = ip;
            }
        } else {
            ++iter;
        }
    }
}
GET_CONFIG_FUNC(std::vector<std::string>, extern_ips, Rtc::kExternIP, [](string str) {
    std::vector<std::string> ret;
    if (str.length()) {
        ret = split(str, ",");
    }
    translateIPFromEnv(ret);
    return ret;
});

static std::atomic<uint64_t> s_key{0};
WebRtcTransport::WebRtcTransport(const EventPoller::Ptr &poller) {
    _poller = poller;
    _identifier = "zlm_" + std::to_string(++s_key);
    _packet_pool.setSize(64);
}

void WebRtcTransport::onCreate() {
    _dtls_transport = std::make_shared<RTC::DtlsTransport>(_poller, this);
    _ice_server = std::make_shared<RTC::IceServer>(this, _identifier, makeRandStr(24));
}

void WebRtcTransport::onDestory() {
#ifdef ENABLE_SCTP
    _sctp = nullptr;
#endif
    _dtls_transport = nullptr;
    _ice_server = nullptr;
}

const EventPoller::Ptr &WebRtcTransport::getPoller() const {
    return _poller;
}

const string &WebRtcTransport::getIdentifier() const {
    return _identifier;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebRtcTransport::OnIceServerSendStunPacket(
    const RTC::IceServer *iceServer, const RTC::StunPacket *packet, RTC::TransportTuple *tuple) {
    // dtls包默认都发往getSelectedTuple，即ice选择的候选地址
    sendSockData((char *) packet->GetData(), packet->GetSize(), tuple);
}

void WebRtcTransport::OnIceServerSelectedTuple(const RTC::IceServer *iceServer, RTC::TransportTuple *tuple) {
    InfoT << getPeerAddress(tuple);
}

void WebRtcTransport::OnIceServerConnected(const RTC::IceServer *iceServer) {
    InfoT;
}

void WebRtcTransport::OnIceServerCompleted(const RTC::IceServer *iceServer) {
    InfoT;
    // ice建联完毕后，启动dtls协商
    if (_answer_sdp->media[0].role == DtlsRole::passive) {
        _dtls_transport->Run(RTC::DtlsTransport::Role::SERVER);
    } else {
        _dtls_transport->Run(RTC::DtlsTransport::Role::CLIENT);
    }
}

void WebRtcTransport::OnIceServerDisconnected(const RTC::IceServer *iceServer) {
    InfoT;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebRtcTransport::OnDtlsTransportConnected(
        const RTC::DtlsTransport *dtlsTransport,
        RTC::SrtpSession::CryptoSuite srtpCryptoSuite,
        uint8_t *srtpLocalKey,
        size_t srtpLocalKeyLen,
        uint8_t *srtpRemoteKey,
        size_t srtpRemoteKeyLen,
        std::string &remoteCert) {
    InfoT;
    // dtls协商完毕后，创建srtp加解密会话
    _srtp_session_send = std::make_shared<RTC::SrtpSession>(RTC::SrtpSession::Type::OUTBOUND, srtpCryptoSuite, srtpLocalKey, srtpLocalKeyLen);
    _srtp_session_recv = std::make_shared<RTC::SrtpSession>(RTC::SrtpSession::Type::INBOUND, srtpCryptoSuite, srtpRemoteKey, srtpRemoteKeyLen);
#ifdef ENABLE_SCTP
    _sctp = std::make_shared<RTC::SctpAssociationImp>(getPoller(), this, 128, 128, 262144, true);
    _sctp->TransportConnected();
#endif
    onStartWebRTC();
}

void WebRtcTransport::OnDtlsTransportSendData(
    const RTC::DtlsTransport *dtlsTransport, const uint8_t *data, size_t len) {
    // dtls包默认都发往getSelectedTuple，即ice选择的候选地址
    sendSockData((char *)data, len, nullptr);
}

void WebRtcTransport::OnDtlsTransportConnecting(const RTC::DtlsTransport *dtlsTransport) {
    InfoT;
}

void WebRtcTransport::OnDtlsTransportFailed(const RTC::DtlsTransport *dtlsTransport) {
    InfoT;
    onShutdown(SockException(Err_shutdown, "dtls transport failed"));
}

void WebRtcTransport::OnDtlsTransportClosed(const RTC::DtlsTransport *dtlsTransport) {
    InfoT;
    onShutdown(SockException(Err_shutdown, "dtls close notify received"));
}

void WebRtcTransport::OnDtlsTransportApplicationDataReceived(
    const RTC::DtlsTransport *dtlsTransport, const uint8_t *data, size_t len) {
#ifdef ENABLE_SCTP
    _sctp->ProcessSctpData(data, len);
#else
    InfoT << hexdump(data, len);
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef ENABLE_SCTP
void WebRtcTransport::OnSctpAssociationConnecting(RTC::SctpAssociation *sctpAssociation) {
    TraceT;
}

void WebRtcTransport::OnSctpAssociationConnected(RTC::SctpAssociation *sctpAssociation) {
    InfoT;
}

void WebRtcTransport::OnSctpAssociationFailed(RTC::SctpAssociation *sctpAssociation) {
    WarnT;
}

void WebRtcTransport::OnSctpAssociationClosed(RTC::SctpAssociation *sctpAssociation) {
    InfoT;
}

void WebRtcTransport::OnSctpAssociationSendData(
    RTC::SctpAssociation *sctpAssociation, const uint8_t *data, size_t len) {
    _dtls_transport->SendApplicationData(data, len);
}

void WebRtcTransport::OnSctpAssociationMessageReceived(
    RTC::SctpAssociation *sctpAssociation, uint16_t streamId, uint32_t ppid, const uint8_t *msg, size_t len) {
    InfoT << streamId << " " << ppid << " " << len << " " << string((char *)msg, len);
    RTC::SctpStreamParameters params;
    params.streamId = streamId;
    // 回显数据
    _sctp->SendSctpMessage(params, ppid, msg, len);
}
#endif
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebRtcTransport::sendSockData(const char *buf, size_t len, RTC::TransportTuple *tuple){
    auto pkt = _packet_pool.obtain2();
    pkt->assign(buf, len);
    if (!tuple) tuple = _ice_server->GetSelectedTuple();
    onSendSockData(std::move(pkt), true, tuple);
}

RTC::TransportTuple *WebRtcTransport::getSelectedTuple() const {
    return _ice_server->GetSelectedTuple();
}

void WebRtcTransport::sendRtcpRemb(uint32_t ssrc, size_t bit_rate) {
    auto remb = FCI_REMB::create({ ssrc }, (uint32_t)bit_rate);
    auto fb = RtcpFB::create(PSFBType::RTCP_PSFB_REMB, remb.data(), remb.size());
    fb->ssrc = htonl(0);
    fb->ssrc_media = htonl(ssrc);
    sendRtcpPacket((char *)fb.get(), fb->getSize(), true);
}

void WebRtcTransport::sendRtcpPli(uint32_t ssrc) {
    auto pli = RtcpFB::create(PSFBType::RTCP_PSFB_PLI);
    pli->ssrc = htonl(0);
    pli->ssrc_media = htonl(ssrc);
    sendRtcpPacket((char *)pli.get(), pli->getSize(), true);
}

// 返回algorithm_str类型的全局本地共享密钥值，GetLocalFingerprints启动时初始化后就不变
string getFingerprint(const string &algorithm_str, const std::shared_ptr<RTC::DtlsTransport> &transport){
    // str to enum
    auto algorithm = RTC::DtlsTransport::GetFingerprintAlgorithm(algorithm_str);
    for (auto &finger_prints : transport->GetLocalFingerprints()) {
        if (finger_prints.algorithm == algorithm) {
            return finger_prints.value;
        }
    }
    throw std::invalid_argument("不支持的加密算法:" + algorithm_str);
}

void WebRtcTransport::setRemoteDtlsFingerprint(const RtcSession &remote){
    //设置远端dtls签名
    auto fingerprint = remote.media[0].fingerprint;
    RTC::DtlsTransport::Fingerprint remote_fingerprint;
    remote_fingerprint.algorithm = RTC::DtlsTransport::GetFingerprintAlgorithm(fingerprint.algorithm);
    remote_fingerprint.value = fingerprint.hash;
    _dtls_transport->SetRemoteFingerprint(remote_fingerprint);
}

void WebRtcTransport::onRtcConfigure(RtcConfigure &configure) const {
    //开启remb后关闭twcc，twcc和remb只能开启一个
    GET_CONFIG(size_t, remb_bit_rate, Rtc::kRembBitRate);
    configure.enableTWCC(!remb_bit_rate);
}

std::string WebRtcTransport::getAnswerSdp(const string &offer) {
    try {
        //// 解析offer sdp ////
        _offer_sdp = std::make_shared<RtcSession>();
        _offer_sdp->loadFrom(offer);
        onCheckSdp(SdpType::offer, *_offer_sdp);
        _offer_sdp->checkValid();

        setRemoteDtlsFingerprint(*_offer_sdp);

        //// sdp 配置 ////
        SdpAttrFingerprint fingerprint;
        fingerprint.algorithm = _offer_sdp->media[0].fingerprint.algorithm;
        fingerprint.hash = getFingerprint(fingerprint.algorithm, _dtls_transport);

        RtcConfigure configure;
        configure.setDefaultSetting(
            _ice_server->GetUsernameFragment(), _ice_server->GetPassword(), RtpDirection::sendrecv, fingerprint);
        onRtcConfigure(configure);

        //// 生成answer sdp ////
        _answer_sdp = configure.createAnswer(*_offer_sdp);
        onCheckSdp(SdpType::answer, *_answer_sdp);
        _answer_sdp->checkValid();
        return _answer_sdp->toString();
    } catch (std::exception &ex) {
        onShutdown(SockException(Err_shutdown, ex.what()));
        throw;
    }
}

static bool is_dtls(char *buf) {
    return ((*buf > 19) && (*buf < 64));
}

static bool is_rtp(char *buf) {
    RtpHeader *header = (RtpHeader *)buf;
    return ((header->pt < 64) || (header->pt >= 96));
}

static bool is_rtcp(char *buf) {
    RtpHeader *header = (RtpHeader *)buf;
    return ((header->pt >= 64) && (header->pt < 96));
}


void WebRtcTransport::inputSockData(char *buf, int len, RTC::TransportTuple *tuple) {
    if (RTC::StunPacket::IsStun((const uint8_t *) buf, len)) {
        std::unique_ptr<RTC::StunPacket> packet(RTC::StunPacket::Parse((const uint8_t *) buf, len));
        if (!packet) {
            WarnT << "parse stun error from " << getPeerAddress(tuple) << ", len=" << len;
            return;
        }
        _ice_server->ProcessStunPacket(packet.get(), tuple);
        return;
    }
    if (is_dtls(buf)) {
        _dtls_transport->ProcessDtlsData((uint8_t *) buf, len);
        return;
    }
    if (is_rtp(buf)) {
        if (!_srtp_session_recv) {
            WarnT << "received rtp packet when dtls not completed from:" << getPeerAddress(tuple);
            return;
        }
        if (_srtp_session_recv->DecryptSrtp((uint8_t *) buf, &len)) {
            if(dumpRtp1) TraceT <<  "recvRtp " << ((const RtpHeader*)buf)->dump(len);
            onRtp(buf, len, _ticker.createdTime());
        }
        return;
    }
    if (is_rtcp(buf)) {
        if (!_srtp_session_recv) {
            WarnL << "received rtcp packet when dtls not completed from:" << getPeerAddress(tuple);
            return;
        }
        if (_srtp_session_recv->DecryptSrtcp((uint8_t *) buf, &len)) {
            onRtcp(buf, len);
        }
        return;
    }
}

void WebRtcTransport::sendRtpPacket(const char *buf, int len, bool flush, void *ctx) {
    if (_srtp_session_send) {
        auto pkt = _packet_pool.obtain2();
        //预留rtx加入的两个字节
        pkt->setCapacity((size_t) len + SRTP_MAX_TRAILER_LEN + 2);
        pkt->assign(buf, len);
        // 回调发送的明文Rtp数据
        onBeforeEncryptRtp(pkt->data(), len, ctx);
        if (dumpRtp1) TraceT << "sendRtp " << ((const RtpHeader*)buf)->dump(len);

        // 加密
        if (_srtp_session_send->EncryptRtp(reinterpret_cast<uint8_t *>(pkt->data()), &len)) {
            pkt->setSize(len);
            // 发送
            onSendSockData(std::move(pkt), flush);
        }
    }
}

void WebRtcTransport::sendRtcpPacket(const char *buf, int len, bool flush, void *ctx) {
    if (dumpRtcp) TraceT <<  "sendRtcp " << ((const RtcpHeader*)buf)->dump(len);
    if (_srtp_session_send) {
        auto pkt = _packet_pool.obtain2();
        // 预留rtx加入的两个字节
        pkt->setCapacity((size_t)len + SRTP_MAX_TRAILER_LEN + 2);
        pkt->assign(buf, len);
        // 回调发送的明文Rtcp数据
        onBeforeEncryptRtcp(pkt->data(), len, ctx);
        // 加密
        if (_srtp_session_send->EncryptRtcp(reinterpret_cast<uint8_t *>(pkt->data()), &len)) {
            pkt->setSize(len);
            // 发送
            onSendSockData(std::move(pkt), flush);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////

WebRtcTransportImp::WebRtcTransportImp(const EventPoller::Ptr &poller) : WebRtcTransport(poller) {
    InfoT;
}

WebRtcTransportImp::~WebRtcTransportImp() {
    InfoT;
}
void WebRtcTransportImp::OnDtlsTransportApplicationDataReceived(const RTC::DtlsTransport *dtlsTransport, const uint8_t *data, size_t len) {
    WebRtcTransport::OnDtlsTransportApplicationDataReceived(dtlsTransport, data, len);
#ifdef ENABLE_SCTP
    if (_answer_sdp->isOnlyDatachannel()) {
        _alive_ticker.resetTime();
    }
#endif
}

uint64_t WebRtcTransportImp::getBytesUsage() const{
    return _bytes_usage;
}

uint64_t WebRtcTransportImp::getDuration() const{
    return _alive_ticker.createdTime() / 1000;
}

void WebRtcTransportImp::updateTicker() {
    _alive_ticker.resetTime();
}

void WebRtcTransportImp::registerSelf() {
    _self = std::static_pointer_cast<WebRtcTransportImp>(shared_from_this());
    WebRtcServer::Instance().addItem(getIdentifier(), _self);
}

void WebRtcTransportImp::unrefSelf() {
    _self = nullptr;
}

void WebRtcTransportImp::unregisterSelf() {
    unrefSelf();
    WebRtcServer::Instance().removeItem(getIdentifier());
}

void WebRtcTransportImp::onCreate(){
    WebRtcTransport::onCreate();
    registerSelf();

    std::weak_ptr<WebRtcTransportImp> weak_self = std::static_pointer_cast<WebRtcTransportImp>(shared_from_this());
    GET_CONFIG(float, timeoutSec, Rtc::kTimeOutSec);
    _timer = std::make_shared<Timer>(timeoutSec / 2, [weak_self]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        if (strong_self->_alive_ticker.elapsedTime() > timeoutSec * 1000) {
            strong_self->onShutdown(SockException(Err_timeout, "rtp/rtcp接收超时"));
            return false;
        }
        return true;
    }, getPoller());

    _twcc_ctx.setOnSendTwccCB([this](uint32_t ssrc, string fci) {
        onSendTwcc(ssrc, fci);
    });
}

void WebRtcTransportImp::onDestory() {
    WebRtcTransport::onDestory();
    unregisterSelf();
}

const Session::Ptr &WebRtcTransportImp::getSession() const {
    return _selected_session;
}

void WebRtcTransportImp::setSession(Session::Ptr session) {
    _history_sessions.emplace(session.get(), session);
    if (_selected_session) {
        InfoT << "rtc network changed: " << _selected_session->peeraddr() << " -> " << session->peeraddr();
    }
    _selected_session = std::move(session);
    unrefSelf();
}

void WebRtcTransportImp::onSendSockData(Buffer::Ptr buf, bool flush, RTC::TransportTuple *tuple) {
    if (!_selected_session) {
        WarnT << "send data failed:" << buf->size();
        return;
    }

    // 一次性发送一帧的rtp数据，提高网络io性能
    if (hio_type(_selected_session->io()) == HIO_TYPE_TCP) {
        // 增加tcp两字节头
        auto len = buf->size();
        char tcp_len[2] = { 0 };
        tcp_len[0] = (len >> 8) & 0xff;
        tcp_len[1] = len & 0xff;
        _selected_session->write(tcp_len, 2);
    }
    _selected_session->write(buf->data(), buf->size());

    if (flush) {
        _selected_session->flush();
    }
}

///////////////////////////////////////////////////////////////////

bool WebRtcTransportImp::canSendRtp() const {
    for (auto &m : _answer_sdp->media) {
        if (m.direction == RtpDirection::sendrecv || m.direction == RtpDirection::sendonly) {
            return true;
        }
    }
    return false;
}

bool WebRtcTransportImp::canRecvRtp() const {
    for (auto &m : _answer_sdp->media) {
        if (m.direction == RtpDirection::sendrecv || m.direction == RtpDirection::recvonly) {
            return true;
        }
    }
    return false;
}

void WebRtcTransportImp::onStartWebRTC() {
    // 获取ssrc和pt相关信息,届时收到rtp和rtcp时分别可以根据pt和ssrc找到相关的信息
    for (auto &m_answer : _answer_sdp->media) {
        if (m_answer.type == TrackApplication) {
            continue;
        }
        auto m_offer = _offer_sdp->getMedia(m_answer.type);
        auto track = std::make_shared<MediaTrack>();

        track->media = &m_answer;
        track->answer_ssrc_rtp = m_answer.getRtpSSRC();
        track->answer_ssrc_rtx = m_answer.getRtxSSRC();
        track->offer_ssrc_rtp = m_offer->getRtpSSRC();
        track->offer_ssrc_rtx = m_offer->getRtxSSRC();
        track->plan_rtp = &m_answer.plan[0];
        track->plan_rtx = m_answer.getRelatedRtxPlan(track->plan_rtp->pt);
        track->rtcp_context_send = std::make_shared<RtcpContextForSend>();

        // rtp track type --> MediaTrack
        if (m_answer.direction == RtpDirection::sendonly || m_answer.direction == RtpDirection::sendrecv) {
            // 该类型的track 才支持发送
            _type_to_track[m_answer.type] = track;
        }
        // send ssrc --> MediaTrack
        _ssrc_to_track[track->answer_ssrc_rtp] = track;
        _ssrc_to_track[track->answer_ssrc_rtx] = track;

        // recv ssrc --> MediaTrack
        _ssrc_to_track[track->offer_ssrc_rtp] = track;
        _ssrc_to_track[track->offer_ssrc_rtx] = track;

        // rtp pt --> MediaTrack
        _pt_to_track[track->plan_rtp->pt].reset(new WrappedRtpTrack(track, _twcc_ctx, *this));
        if (track->plan_rtx) {
            // rtx pt --> MediaTrack
            _pt_to_track[track->plan_rtx->pt].reset(new WrappedRtxTrack(track));
        }

        std::weak_ptr<MediaTrack> weak_track = track;
        // 记录rtp ext类型与id的关系，方便接收或发送rtp时修改rtp ext id
        track->rtp_ext_ctx = std::make_shared<RtpExtContext>(*m_offer);
        track->rtp_ext_ctx->setOnGetRtp([this, weak_track](uint8_t pt, uint32_t ssrc, const string &rid) {
            // ssrc --> MediaTrack
            if (auto track = weak_track.lock()) {
                _ssrc_to_track[ssrc] = std::move(track);
                InfoT << "get rtp, pt:" << (int) pt << ", ssrc:" << ssrc << ", rid:" << rid;
            }
        });

        size_t index = 0;
        for (auto ssrc : m_offer->rtp_ssrc_sim) {
            // 记录ssrc对应的MediaTrack
            _ssrc_to_track[ssrc] = track;
            if (m_offer->rtp_rids.size() > index) {
                // 支持firefox的simulcast, 提前映射好ssrc和rid的关系
                track->rtp_ext_ctx->setRid(ssrc, m_offer->rtp_rids[index]);
            } else {
                // SDP munging没有rid, 它通过group-ssrc:SIM给出ssrc列表;
                // 系统又要有rid，这里手工生成rid，并为其绑定ssrc
                std::string rid = "r" + std::to_string(index);
                track->rtp_ext_ctx->setRid(ssrc, rid);
                if(uint32_t rtx_ssrc = m_offer->getRtxSSRC(ssrc))
                    track->rtp_ext_ctx->setRid(rtx_ssrc, rid);
            }
            ++index;
        }
    }
}

void WebRtcTransportImp::onCheckAnswer(RtcSession &sdp) {
    //修改answer sdp的ip、端口信息
    for (auto &m : sdp.media) {
        m.addr.reset();
        m.addr.address = extern_ips.empty() ? WebRtcServer::Instance().get_local_ip() : extern_ips[0];
        m.rtcp_addr.reset();
        m.rtcp_addr.address = m.addr.address;

        GET_CONFIG(uint16_t, udp_port, Rtc::kPort);
        GET_CONFIG(uint16_t, tcp_port, Rtc::kTcpPort);
        m.rtcp_addr.port = udp_port ? udp_port : tcp_port;
        m.port = m.rtcp_addr.port;
        sdp.origin.address = m.addr.address;
    }

    if (!canSendRtp()) {
        // 设置我们发送的rtp的ssrc
        return;
    }

    for (auto &m : sdp.media) {
        if (m.type == TrackApplication) {
            continue;
        }
        if (!m.rtp_rtx_ssrc.empty()) {
            // 已经生成了ssrc
            continue;
        }
        // 添加answer sdp的ssrc信息
        m.rtp_rtx_ssrc.emplace_back();
        auto &ssrc = m.rtp_rtx_ssrc.back();
        // 发送的ssrc我们随便定义，因为在发送rtp时会修改为此值
        ssrc.ssrc = m.type + RTP_SSRC_OFFSET;
        ssrc.cname = RTP_CNAME;
        ssrc.label = RTP_LABEL;
        ssrc.mslabel = RTP_MSLABEL;
        ssrc.msid = RTP_MSID;

        if (m.getRelatedRtxPlan(m.plan[0].pt)) {
            // rtx ssrc
            ssrc.rtx_ssrc = ssrc.ssrc + RTX_SSRC_OFFSET;
        }
    }
}

void WebRtcTransportImp::onCheckSdp(SdpType type, RtcSession &sdp) {
    switch (type) {
    case SdpType::answer:
        onCheckAnswer(sdp);
        break;
    case SdpType::offer:
        break;
    default: /*不可达*/
        assert(0);
        break;
    }
}

SdpAttrCandidate::Ptr makeIceCandidate(std::string ip, uint16_t port, 
    uint32_t priority = 100, std::string proto = "udp") {
    auto candidate = std::make_shared<SdpAttrCandidate>();
    // rtp端口
    candidate->component = 1;
    candidate->transport = proto;
    candidate->foundation = proto + "candidate";
    // 优先级，单candidate时随便
    candidate->priority = priority;
    candidate->address = ip;
    candidate->port = port;
    candidate->type = "host";
    if (proto == "tcp") {
        candidate->type += " tcptype passive";
    }
    return candidate;
}

void WebRtcTransportImp::onRtcConfigure(RtcConfigure &configure) const {
    WebRtcTransport::onRtcConfigure(configure);

    GET_CONFIG(uint16_t, local_udp_port, Rtc::kPort);
    GET_CONFIG(uint16_t, local_tcp_port, Rtc::kTcpPort);
    if (extern_ips.empty()) {
        std::string local_ip = WebRtcServer::Instance().get_local_ip();
        if (local_udp_port) { configure.addCandidate(*makeIceCandidate(local_ip, local_udp_port, 120, "udp")); }
        if (local_tcp_port) { configure.addCandidate(*makeIceCandidate(local_ip, local_tcp_port, 110, "tcp")); }
    } else {
        const uint32_t delta = 10;
        uint32_t priority = 100 + delta * extern_ips.size();
        for (auto ip : extern_ips) {
            if (local_udp_port) { configure.addCandidate(*makeIceCandidate(ip, local_udp_port, priority + 5, "udp")); }
            if (local_tcp_port) { configure.addCandidate(*makeIceCandidate(ip, local_tcp_port, priority, "tcp")); }
            priority -= delta;
        }
    }
}


///////////////////////////////////////////////////////////////////
// 负责webrtc流的接收，在 RtpTrack 基础上增加 nack请求 和 rtcp rr支持
class RtpChannel : public RtpTrackImp, public std::enable_shared_from_this<RtpChannel> {
public:
    RtpChannel(EventPoller::Ptr poller, RtpTrackImp::OnSorted cb, std::function<void(const FCI_NACK &nack)> on_nack) {
        _poller = std::move(poller);
        setOnSorted(std::move(cb));

        _nack_ctx.setOnNack([this, on_nack](const FCI_NACK &nack) {
            on_nack(nack);
            starNackTimer();
        });
    }

    ~RtpChannel() override = default;
    uint16_t next_seq = 0;
    RtpPacket::Ptr inputRtp(TrackType type, int sample_rate, uint8_t *ptr, size_t len, bool is_rtx) {
        if(!ptr||!len) return nullptr;
        RtpHeader* head = (RtpHeader*)ptr;
        auto seq = ntohs(head->seq);
        /* 乱序检测 
        if (!is_rtx) {
            if(seq != next_seq) TraceL << "seq jump " << next_seq << ": " << head->dump(len);
            next_seq = seq + 1;
        }*/
        auto rtp = RtpTrack::inputRtp(type, sample_rate, ptr, len);
        if (!rtp) {
            TraceL << "skip " << (is_rtx?"rtx":"") << "packet " << head->dump(len);
            return rtp;
        }
        _nack_ctx.received(seq, is_rtx);
        if (!is_rtx) {
            //统计rtp接收情况，便于生成nack rtcp包
            _rtcp_context.onRtp(seq, rtp->getStamp(), rtp->ntp_stamp, sample_rate, len);
        }
        return rtp;
    }

    Buffer::Ptr createRtcpRR(RtcpHeader *sr, uint32_t ssrc) {
        _rtcp_context.onRtcp(sr);
        return _rtcp_context.createRtcpRR(ssrc, getSSRC());
    }

    float getLossRate() {
        auto expected = _rtcp_context.getExpectedPacketsInterval();
        if (!expected) {
            return -1;
        }
        return _rtcp_context.geLostInterval() * 100 / expected;
    }

private:
    void starNackTimer() {
        if (_delay_task) {
            return;
        }
        std::weak_ptr<RtpChannel> weak_self = shared_from_this();
        _delay_task = _poller->setTimeout(10, [weak_self](hv::TimerID tid) {
            if (auto strong_self = weak_self.lock()) {
                uint64_t ret = strong_self->_nack_ctx.reSendNack();
                if (!ret) {
                    strong_self->_delay_task = 0;
                }
                else {
                    hv::resetTimer(tid, ret);
                }
            }
        });
    }

private:
    NackContext _nack_ctx;
    RtcpContextForRecv _rtcp_context;
    EventPoller::Ptr _poller;
    hv::TimerID _delay_task;
};

std::shared_ptr<RtpChannel> MediaTrack::getRtpChannel(uint32_t ssrc) const {
    auto it_chn = rtp_channel.find(rtp_ext_ctx->getRid(ssrc));
    if (it_chn == rtp_channel.end()) {
        return nullptr;
    }
    return it_chn->second;
}

float WebRtcTransportImp::getLossRate(TrackType type) {
    for (auto &pr : _ssrc_to_track) {
        auto ssrc = pr.first;
        auto &track = pr.second;
        auto rtp_chn = track->getRtpChannel(ssrc);
        if (rtp_chn) {
            if (track->media && type == track->media->type) {
                return rtp_chn->getLossRate();
            }
        }
    }
    return -1;
}

void WebRtcTransportImp::onRtcp(const char *buf, size_t len) {
	_bytes_usage += len;
	auto rtcps = RtcpHeader::loadFromBytes((char *)buf, len);
	for (auto rtcp : rtcps) {
		if (dumpRtcp) TraceT << "recvRtcp " << rtcp->dumpString();
		switch ((RtcpType)rtcp->pt) {
		case RtcpType::RTCP_SR: {
			//对方汇报rtp发送情况
			RtcpSR *sr = (RtcpSR *)rtcp;
			auto it = _ssrc_to_track.find(sr->ssrc);
			if (it != _ssrc_to_track.end()) {
				auto &track = it->second;
				auto rtp_chn = track->getRtpChannel(sr->ssrc);
				if (!rtp_chn) {
					WarnT << "未识别的sr rtcp包:" << rtcp->dumpString();
				}
				else {
					//InfoT << "接收丢包率,ssrc:" << sr->ssrc << ",loss rate(%):" << rtp_chn->getLossRate();
					//设置rtp时间戳与ntp时间戳的对应关系
					rtp_chn->setNtpStamp(sr->rtpts, sr->getNtpUnixStampMS());
					auto rr = rtp_chn->createRtcpRR(sr, track->answer_ssrc_rtp);
					sendRtcpPacket(rr->data(), rr->size(), true);
				}
			}
			else {
				WarnT << "未识别的sr rtcp包:" << rtcp->dumpString();
			}
			break;
		}
		case RtcpType::RTCP_RR: {
			_alive_ticker.resetTime();
			//对方汇报rtp接收情况
			RtcpRR *rr = (RtcpRR *)rtcp;
			for (int i = 0; i < rr->count; i++) {
				auto item = rr->getItem(i);
				auto it = _ssrc_to_track.find(item->ssrc);
				if (it != _ssrc_to_track.end()) {
					auto &track = it->second;
					track->rtcp_context_send->onRtcp(rtcp);
					auto sr = track->rtcp_context_send->createRtcpSR(track->answer_ssrc_rtp);
					sendRtcpPacket(sr->data(), sr->size(), true);
				}
				else {
					WarnT << "未识别的rr rtcp包:" << rtcp->dumpString();
				}
			}
			break;
		}
		case RtcpType::RTCP_BYE: {
			// 对方汇报停止发送rtp
			RtcpBye *bye = (RtcpBye *)rtcp;
			for (auto ssrc : bye->getSSRC()) {
				auto it = _ssrc_to_track.find(*ssrc);
				if (it == _ssrc_to_track.end()) {
					WarnT << "未识别的bye rtcp包:" << rtcp->dumpString();
					continue;
				}
				_ssrc_to_track.erase(it);
			}
			onRtcpBye();
			onShutdown(SockException(Err_eof, "rtcp bye message received"));
			break;
		}
		case RtcpType::RTCP_PSFB:
		case RtcpType::RTCP_RTPFB: {
			if ((RtcpType)rtcp->pt == RtcpType::RTCP_PSFB) {
				break;
			}
			//RTPFB
			switch ((RTPFBType)rtcp->count) {
			case RTPFBType::RTCP_RTPFB_NACK: {
				RtcpFB *fb = (RtcpFB *)rtcp;
				auto it = _ssrc_to_track.find(fb->ssrc_media);
				if (it == _ssrc_to_track.end()) {
					WarnT << "未识别的 rtcp包:" << rtcp->dumpString();
					return;
				}
				auto &track = it->second;
				auto &fci = fb->getFci<FCI_NACK>();
				// 收到nack请求，直接重传
				track->nack_list.forEach(fci, [&](const RtpPacket::Ptr &rtp) {
					if (dumpNack) TraceL << getIdentifier() << " sendRtx " << rtp->dump();
					//rtp重传
					onSendRtp(rtp, true, true);
					});
				break;
			}
			default: break;
			}
			break;
		}
		default: break;
		}
	}
}

///////////////////////////////////////////////////////////////////

void WebRtcTransportImp::createRtpChannel(const string &rid, uint32_t ssrc, MediaTrack &track) {
    std::weak_ptr<WebRtcTransportImp> weak_self = std::dynamic_pointer_cast<WebRtcTransportImp>(shared_from_this());
    //rid --> RtpReceiverImp
    track.rtp_channel[rid] = std::make_shared<RtpChannel>(getPoller(), 
        [&track, this, rid](RtpPacket::Ptr rtp) mutable {
            onSortedRtp(track, rid, std::move(rtp));
        }, 
        [&track, weak_self, ssrc](const FCI_NACK &nack) mutable {
            //nack发送可能由定时器异步触发
            if (auto strong_self = weak_self.lock()) {
                strong_self->onSendNack(track, nack, ssrc);
            }
        });
    InfoT << "create rtp receiver of ssrc:" << ssrc << ", rid:" << rid << ", codec:" << track.plan_rtp->codec;
}

/*
 WebRtcTransportImp::onRtp -> WrappedRtpTrack::inputRtp -> RtpChannel::inputRtp -> WebRtcTransportImp::onSortedRtp
     + _pt_to_track             + filter ext                    + nack              + do pli/remb on videotrack
                                + process twcc                  + rtcp rr           + call WebRtcTransportImp::onRecvRtp
                                + auto create RtpChannel        + sort rtp
                            -> WrappedRtxTrack::inputRtp
                                + filter ext
                                + do RtxDecode
*/
void WebRtcTransportImp::onRtp(const char *buf, size_t len, uint64_t stamp_ms) {
    _bytes_usage += len;
    _alive_ticker.resetTime();

    RtpHeader *rtp = (RtpHeader *)buf;
    // 根据接收到的rtp的pt信息，找到该流的信息
    auto it = _pt_to_track.find(rtp->pt);
    if (it == _pt_to_track.end()) {
        WarnT << "unknown rtp pt:" << (int)rtp->pt;
        return;
    }
    it->second->inputRtp(buf, len, stamp_ms, rtp);
}

void WrappedRtpTrack::inputRtp(const char *buf, size_t len, uint64_t stamp_ms, RtpHeader *rtp) {
#if 0
    auto seq = ntohs(rtp->seq);
    if (track->media->type == TrackVideo && seq % 100 == 0) {
        //此处模拟接收丢包
        return;
    }
#endif

    auto ssrc = ntohl(rtp->ssrc);

    // 修改ext id至统一
    string rid;
    auto twcc_ext = track->rtp_ext_ctx->changeRtpExtId(rtp, true, &rid, RtpExtType::transport_cc);
    if (twcc_ext) {
        _twcc_ctx.onRtp(ssrc, twcc_ext.getTransportCCSeq(), stamp_ms);
    }

    auto &ref = track->rtp_channel[rid];
    if (!ref) {
        _transport.createRtpChannel(rid, ssrc, *track);
    }

    // 解析并排序rtp
    ref->inputRtp(track->media->type, track->plan_rtp->sample_rate, (uint8_t *)buf, len, false);
}

void WrappedRtxTrack::inputRtp(const char *buf, size_t len, uint64_t stamp_ms, RtpHeader *rtp) {
    // 修改ext id至统一
    string rid;
    track->rtp_ext_ctx->changeRtpExtId(rtp, true, &rid, RtpExtType::transport_cc);

    auto &ref = track->rtp_channel[rid];
    if (!ref) {
        // 再接收到对应的rtp前，丢弃rtx包
        WarnL << "unknown rtx rtp, rid:" << rid << ", ssrc:" << ntohl(rtp->ssrc) << ", codec:" << track->plan_rtp->codec
              << ", seq:" << ntohs(rtp->seq);
        return;
    }

    // 这里是rtx重传包
    // https://datatracker.ietf.org/doc/html/rfc4588#section-4
    auto payload = rtp->getPayloadData();
    auto size = rtp->getPayloadSize(len);
    if (size < 2) {
        return;
    }

    // 前两个字节是原始的rtp的seq
    auto origin_seq = payload[0] << 8 | payload[1];
    // rtx 转换为 rtp
    rtp->pt = track->plan_rtp->pt;
    rtp->seq = htons(origin_seq);
    rtp->ssrc = htonl(ref->getSSRC());

    memmove((uint8_t *)buf + 2, buf, payload - (uint8_t *)buf);
    buf += 2;
    len -= 2;
    ref->inputRtp(track->media->type, track->plan_rtp->sample_rate, (uint8_t *)buf, len, true);
}

void WebRtcTransportImp::onSendNack(MediaTrack &track, const FCI_NACK &nack, uint32_t ssrc) {
    if(dumpNack) TraceT << ssrc << " send nack " << nack.dumpString();
    auto rtcp = RtcpFB::create(RTPFBType::RTCP_RTPFB_NACK, &nack, FCI_NACK::kSize);
    rtcp->ssrc = htonl(track.answer_ssrc_rtp);
    rtcp->ssrc_media = htonl(ssrc);
    sendRtcpPacket((char *)rtcp.get(), rtcp->getSize(), true);
}

void WebRtcTransportImp::onSendTwcc(uint32_t ssrc, const string &twcc_fci) {
    auto rtcp = RtcpFB::create(RTPFBType::RTCP_RTPFB_TWCC, twcc_fci.data(), twcc_fci.size());
    rtcp->ssrc = htonl(0);
    rtcp->ssrc_media = htonl(ssrc);
    sendRtcpPacket((char *)rtcp.get(), rtcp->getSize(), true);
}

///////////////////////////////////////////////////////////////////

void WebRtcTransportImp::onSortedRtp(MediaTrack &track, const string &rid, RtpPacket::Ptr rtp) {
    if (track.media->type == TrackVideo && _pli_ticker.elapsedTime() > 2000) {
        // 定期发送pli请求关键帧，方便非rtc等协议
        _pli_ticker.resetTime();
        sendRtcpPli(rtp->getSSRC());

        // 开启remb，则发送remb包调节比特率
        GET_CONFIG(size_t, remb_bit_rate, Rtc::kRembBitRate);
        if (remb_bit_rate && _answer_sdp->supportRtcpFb(SdpConst::kRembRtcpFb)) {
            sendRtcpRemb(rtp->getSSRC(), remb_bit_rate);
        }
    }
    if (dumpRtp2) TraceT << rid << " onSortedRtp " << rtp->dump();
    onRecvRtp(track, rid, std::move(rtp));
}

/*
WebRtcTransportImp::onSendRtp -> WebRtcTransport::sendRtpPacket
    + update rtcp context          + WebRtcTransport::onBeforeEncryptRtp   ->  WebRtcTransportImp::onBeforeEncryptRtp
    + save in nack_list            + srtp::EncryptRtp                           + changeRtpExtId
                                   + WebRtcTransport::onSendSockData            + rtxEncode
*/
void WebRtcTransportImp::onSendRtp(const RtpPacket::Ptr &rtp, bool flush, bool rtx) {
    if (dumpRtp2) TraceT << (rtx?"onSendRtx ":"onSendRtp ") << rtp->dump() << " flush:" << flush;
    auto &track = _type_to_track[rtp->type];
    if (!track) {
        // 忽略，对方不支持该编码类型
        return;
    }
    int len = rtp->size() - RtpPacket::kRtpTcpHeaderSize;
    if (!rtx) {
        // 统计rtp发送情况，好做sr汇报
        track->rtcp_context_send->onRtp(rtp->getSeq(), rtp->getStamp(), rtp->ntp_stamp, rtp->sample_rate, len);
        track->nack_list.pushBack(rtp);
#if 0
        //此处模拟发送丢包
        if (rtp->type == TrackVideo && rtp->getSeq() % 100 == 0) {
            return;
        }
#endif
    } else {
        // 发送rtx重传包
        // TraceL << "send rtx rtp:" << rtp->getSeq();
    }
    std::pair<bool/*rtx*/, MediaTrack *> ctx{rtx, track.get()};
    sendRtpPacket(rtp->data() + RtpPacket::kRtpTcpHeaderSize, len, flush, &ctx);
    _bytes_usage += len;
}

void WebRtcTransportImp::onBeforeEncryptRtp(const char *buf, int &len, void *ctx) {
    if(!ctx) return ;
    auto pr = (std::pair<bool/*rtx*/, MediaTrack *> *) ctx;
    MediaTrack* track = pr->second;
    auto header = (RtpHeader *) buf;

    if (!pr->first || !track->plan_rtx) {
        // 普通的rtp,或者不支持rtx, 修改目标pt和ssrc
        track->rtp_ext_ctx->changeRtpExtId(header, false);
        header->pt = track->plan_rtp->pt;
        header->ssrc = htonl(track->answer_ssrc_rtp);
    } else {
        // 重传的rtp, rtx
        track->rtp_ext_ctx->changeRtpExtId(header, false);
        header->pt = track->plan_rtx->pt;
        if (track->answer_ssrc_rtx) {
            // 有rtx单独的ssrc,有些情况下，浏览器支持rtx，但是未指定rtx单独的ssrc
            header->ssrc = htonl(track->answer_ssrc_rtx);
        } else {
            // 未单独指定rtx的ssrc，那么使用rtp的ssrc
            header->ssrc = htonl(track->answer_ssrc_rtp);
        }

        auto origin_seq = ntohs(header->seq);
        // seq跟原来的不一样
        header->seq = htons(_rtx_seq[track->media->type]);
        ++_rtx_seq[track->media->type];

        auto payload = header->getPayloadData();
        auto payload_size = header->getPayloadSize(len);
        if (payload_size) {
            // rtp负载后移两个字节，这两个字节用于存放osn
            // https://datatracker.ietf.org/doc/html/rfc4588#section-4
            memmove(payload + 2, payload, payload_size);
        }
        payload[0] = origin_seq >> 8;
        payload[1] = origin_seq & 0xFF;
        len += 2;
    }
}

void WebRtcTransportImp::onShutdown(const SockException &ex){
    WarnL << ex.what();
    unrefSelf();
    // 关闭所有udp会话
    for (auto &pr : _history_sessions) {
        auto session = pr.second.lock();
        if (session) {
            session->shutdown(ex);
        }
    }
}

void WebRtcTransportImp::onRtcpBye(){}

/////////////////////////////////////////////////////////////////////////////////////////////

void echo_plugin(hv::SocketChannelPtr sender, const WebRtcArgs &args, const WebRtcServer::onCreateRtc &cb) {
    cb(*WebRtcEchoTest::create(WebRtcServer::Instance().getPoller()));
}

void push_plugin(hv::SocketChannelPtr sender, const WebRtcArgs &args, const WebRtcServer::onCreateRtc &cb) {
    MediaInfo info(args.at("url"));
    Broadcast::PublishAuthInvoker invoker = [cb, info](const string &err, const ProtocolOption &option) mutable {
        if (!err.empty()) {
            cb(WebRtcException(SockException(Err_other, err)));
            return;
        }

        RtspMediaSourceImp::Ptr push_src;
        std::shared_ptr<void> push_src_ownership;
        auto src = MediaSource::find(RTC_SCHEMA, info._vhost, info._app, info._streamid);
        auto push_failed = (bool)src;

        while (src) {
            // 尝试断连后继续推流
            auto rtsp_src = std::dynamic_pointer_cast<RtspMediaSourceImp>(src);
            if (!rtsp_src) {
                // 源不是rtsp推流产生的
                break;
            }
            auto ownership = rtsp_src->getOwnership();
            if (!ownership) {
                // 获取推流源所有权失败
                break;
            }
            push_src = std::move(rtsp_src);
            push_src_ownership = std::move(ownership);
            push_failed = false;
            break;
        }

        if (push_failed) {
            cb(WebRtcException(SockException(Err_other, "already publishing")));
            return;
        }

        if (!push_src) {
            push_src = std::make_shared<RtcMediaSourceImp>(info._vhost, info._app, info._streamid);
            push_src_ownership = push_src->getOwnership();
            push_src->setProtocolOption(option);
        }
        auto rtc = WebRtcPusher::create(WebRtcServer::Instance().getPoller(), push_src, push_src_ownership, info, option);
        push_src->setListener(rtc);
        cb(*rtc);
    };

    // rtsp推流需要鉴权
    auto flag = NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastMediaPublish, MediaOriginType::rtc_push, info, invoker, sender);
    if (!flag) {
        // 该事件无人监听,默认不鉴权
        invoker("", ProtocolOption());
    }
}

void play_plugin(hv::SocketChannelPtr sender, const WebRtcArgs &args, const WebRtcServer::onCreateRtc &cb) {
    MediaInfo info(args.at("url"));
    Broadcast::AuthInvoker invoker = [cb, info, sender](const string &err) mutable {
        if (!err.empty()) {
            cb(WebRtcException(SockException(Err_other, err)));
            return;
        }

        // webrtc播放的是rtsp的源
        info._schema = RTC_SCHEMA;
        MediaSource::findAsync(info, sender, [=](const MediaSource::Ptr &src_in) mutable {
            auto src = std::dynamic_pointer_cast<RtspMediaSource>(src_in);
            if (!src) {
                cb(WebRtcException(SockException(Err_other, "stream not found")));
                return;
            }
            // 还原成rtc，目的是为了hook时识别哪种播放协议
            info._schema = RTC_SCHEMA;
            auto rtc = WebRtcPlayer::create(WebRtcServer::Instance().getPoller(), src, info);
            cb(*rtc);
        });
    };

    //广播通用播放url鉴权事件
    auto flag = NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastMediaPlayed, info, invoker, sender);
    if (!flag) {
        //该事件无人监听,默认不鉴权
        invoker("");
    }
}

static onceToken s_rtc_auto_register([]() {
    WebRtcServer::Instance().registerPlugin("echo", echo_plugin);
    WebRtcServer::Instance().registerPlugin("push", push_plugin);
    WebRtcServer::Instance().registerPlugin("play", play_plugin);
});

}// namespace mediakit