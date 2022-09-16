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
#include "webrtc/IceServer.hpp"
#include "webrtc/WebRtcTransport.h"
#include "FMP4/FMP4MediaSource.h"
#include "Rtmp/RtmpMediaSource.h"
#include "TS/TSMediaSource.h"
#include "Util/File.h"
#include "hstring.h"
#include "hbase.h"
#include <string>
#include <iostream>
#include "Common/config.h"
#include "../server/WebHook.h"
#include "Camera/CameraManager.h"

using namespace std;
using namespace mediakit;
using namespace toolkit;
using AutoLock = std::lock_guard<std::recursive_mutex>;
std::string g_ini_file;
#if defined(ENABLE_RTPPROXY)
#include "Rtp/RtpServer.h"
#include "Rtp/RtpSelector.h"
static std::unordered_map<std::string, RtpServer::Ptr> s_rtpServerMap;
static std::recursive_mutex s_rtpServerMapMtx;
uint16_t openRtpServer(uint16_t local_port, const std::string &stream_id, bool enable_tcp, const std::string &local_ip, bool re_use_port, uint32_t ssrc) {
    AutoLock lck(s_rtpServerMapMtx);
    if (s_rtpServerMap.find(stream_id) != s_rtpServerMap.end()) {
        //为了防止RtpProcess所有权限混乱的问题，不允许重复添加相同的stream_id
        return 0;
    }

    RtpServer::Ptr server = std::make_shared<RtpServer>();
    server->start(local_port, stream_id, RtpServer::PASSIVE, local_ip.c_str(), re_use_port, ssrc);
    server->setOnDetach([stream_id]() {
        //设置rtp超时移除事件
        AutoLock lck(s_rtpServerMapMtx);
        s_rtpServerMap.erase(stream_id);
        });

    //保存对象
    s_rtpServerMap.emplace(stream_id, server);
    //回复json
    return server->getPort();
}

bool closeRtpServer(const std::string &stream_id) {
    AutoLock lck(s_rtpServerMapMtx);
    auto it = s_rtpServerMap.find(stream_id);
    if (it == s_rtpServerMap.end()) {
        return false;
    }
    auto server = it->second;
    s_rtpServerMap.erase(it);
    return true;
}
#endif

#include "md5.h"
#include "Pusher/PusherProxy.h"
#include "Player/PlayerProxy.h"
//拉流代理器列表
static std::unordered_map<string, PlayerProxy::Ptr> s_proxyMap;
static std::recursive_mutex s_proxyMapMtx;

//推流代理器列表
static std::unordered_map<string, PusherProxy::Ptr> s_proxyPusherMap;
static std::recursive_mutex s_proxyPusherMapMtx;
static inline string getProxyKey(const string &vhost, const string &app, const string &stream) {
    return vhost + "/" + app + "/" + stream;
}

static inline string getPusherKey(const string &schema, const string &vhost, const string &app, const string &stream,
    const string &dst_url) {
    return schema + "/" + vhost + "/" + app + "/" + stream + "/" + Md5Str(dst_url);
}

//RTC配置项目
namespace RTC {
#define RTC_FIELD "rtc."
//服务器外网ip
const string kHttpPort = RTC_FIELD"httpPort";
//设置remb比特率，非0时关闭twcc并开启remb。该设置在rtc推流时有效，可以控制推流画质
const string kHttpsPort = RTC_FIELD"httpsPort";
const string kCertFile = RTC_FIELD"certFile";
const string kKeyFile = RTC_FIELD"keyFile";

static onceToken token([]() {
    mINI::Instance()[kCertFile] = "cert/server.crt";
    mINI::Instance()[kKeyFile] = "cert/server.key";
    mINI::Instance()[kHttpPort] = 80;
    mINI::Instance()[kHttpsPort] = 443;
});
}//namespace RTC
////////////RTSP服务器配置///////////
namespace Rtsp {
#define RTSP_FIELD "rtsp."
const string kPort = RTSP_FIELD"port";
const string kSSLPort = RTSP_FIELD"sslport";
onceToken token1([]() {
    mINI::Instance()[kPort] = 554;
    mINI::Instance()[kSSLPort] = 332;
}, nullptr);

} //namespace Rtsp

////////////RTMP服务器配置///////////
namespace Rtmp {
#define RTMP_FIELD "rtmp."
const string kPort = RTMP_FIELD"port";
const string kSSLPort = RTMP_FIELD"sslport";
onceToken token1([]() {
    mINI::Instance()[kPort] = 1935;
    mINI::Instance()[kSSLPort] = 19350;
}, nullptr);
} //namespace RTMP

//////////////////////////////////////////////////////////////////////////////////
WebRtcServer::WebRtcServer() {
}

WebRtcServer::~WebRtcServer() {
    stop();
}

WebRtcServer& WebRtcServer::Instance()
{
    static WebRtcServer instance;
    return instance;
}

hv::EventLoopPtr WebRtcServer::getPoller()
{
    return EventLoopThreadPool::Instance()->nextLoop();
}

#ifdef WIN32
void logview_logger(int loglevel, const char* buf, int len) {
    fprintf(stdout, "%.*s", len, buf);
    static HWND hWndLogView = NULL;
    static DWORD nLastTick = 0;
    long nTickCount = GetTickCount();
    //static std::mutex csLock;
    //std::unique_lock<std::mutex> l(csLock);
    if ((nTickCount - nLastTick) > (30 * 1000)) {
        hWndLogView = ::FindWindowA("Coobol_LogView", NULL);
        nLastTick = nTickCount;
    }
    if (hWndLogView) {
        static const char* pLevel = "TDWE";
        COPYDATASTRUCT Data;
        Data.dwData = 0;
        Data.lpData = (void*)buf;
        Data.cbData = len;
        ::SendMessage(hWndLogView, WM_COPYDATA, 0, (LPARAM)&Data);
        Data.cbData = 0;
    } else if((nTickCount - nLastTick) > 1000) {
        hWndLogView = ::FindWindowA("Coobol_LogView", NULL);
        nLastTick = nTickCount;
    }    
}
#endif

void WebRtcServer::start(const char* cfgPath)
{
    if (cfgPath) {
        g_ini_file = cfgPath;
        mediakit::loadIniConfig(g_ini_file.c_str());
    }

    int loglevel = LOG_LEVEL_DEBUG;
    auto it = mINI::Instance().find("app.loglevel");
    if(it != mINI::Instance().end())
        loglevel = it->second;
    hlog_set_level(loglevel);
    // [09-12 09:07:13.574] [259] INFO(FlvPlayer.cpp:348): come in stop      
    hlog_set_format("[%m-%d %H:%M:%S.%z] [%t] %F(%f:%n %l): %s");
    //hlog_set_format("[%m-%d %H:%M:%S.%z] [%t] %L(%f:%n): %s");
#ifdef WIN32
    hlog_set_handler(logview_logger);
#else
    hlog_set_handler(stdout_logger);
#endif
    it = mINI::Instance().find("app.threads");
    if(it != mINI::Instance().end())
        EventLoopThreadPool::Instance()->setThreadNum(it->second);
    EventLoopThreadPool::Instance()->start();

    uint16_t port = mINI::Instance()[Rtc::kPort];
    if (port)
       _udpRtc = newRtcServer(port);

    port = mINI::Instance()[Rtc::kTcpPort];
    if (port)
        _tcpRtc = newRtcTcpServer(port);

    port = mINI::Instance()["srt.port"];
    if (port)
        _udpSrt = newSrtServer(port);

    port = mINI::Instance()[::Rtmp::kPort];
    if (port)
        _rtmp = newRtmpServer(port);

    port = mINI::Instance()[::Rtsp::kPort];
    if (port)
        _rtsp = newRtspServer(port);

    startHttp();
    installWebHook();
    CameraManager::Instance().Init();
}

void WebRtcServer::stop()
{
    CameraManager::Instance().Destroy();
    _rtsp = nullptr;
    _rtmp = nullptr;
    websocket_server_stop(&_http);
    _udpRtc = nullptr;
    _tcpRtc = nullptr;
    _udpSrt = nullptr;

#if defined(ENABLE_RTPPROXY)
    {
        RtpSelector::Instance().clear();
        AutoLock lck(s_rtpServerMapMtx);
        s_rtpServerMap.clear();
    }
#endif
    {
        AutoLock lck(s_proxyMapMtx);
        s_proxyMap.clear();
    }
    {
        AutoLock lck(s_proxyPusherMapMtx);
        s_proxyPusherMap.clear();
    }
    unInstallWebHook();
    EventLoopThreadPool::Instance()->stop();
}

void WebRtcServer::detect_local_ip()
{
    sockaddr_u localAddr, remoteAddr;
    sockaddr_set_ipport(&remoteAddr, "8.8.8.8", 1234);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    connect(fd, (sockaddr*)&remoteAddr, sockaddr_len(&remoteAddr));
    socklen_t len = sizeof(localAddr);
    getsockname(fd, (sockaddr*)&localAddr, &len);
    char ip[32];
    this->_local_ip = sockaddr_ip(&localAddr, ip, 32);
    LOGI("detect localAddr %s", ip);
}

void WebRtcServer::registerPlugin(const std::string &type, Plugin cb) {
    std::lock_guard<std::mutex> lck(_mtx_creator);
    _map_creator[type] = std::move(cb);
}

WebRtcTransportPtr WebRtcServer::getItem(const std::string &key) {
    WebRtcTransportImp::Ptr ret;
    if (key.empty()) {
        return ret;
    }
    std::lock_guard<std::mutex> lck(_mtx);
    auto it = _map.find(key);
    if (it != _map.end()) {
        ret = it->second.lock();
    }
    return ret;
}

#ifdef ENABLE_MEM_DEBUG
extern uint64_t getTotalMemUsage();
extern uint64_t getTotalMemBlock();
extern uint64_t getThisThreadMemUsage();
extern uint64_t getThisThreadMemBlock();
extern std::vector<size_t> getBlockTypeSize();
extern uint64_t getTotalMemBlockByType(int type);
extern uint64_t getThisThreadMemBlockByType(int type);
#endif

#include "Rtmp/Rtmp.h"
#include "Common/MultiMediaSourceMuxer.h"
void getStatisticJson(const std::function<void(hv::Json &val)> &cb) {
    hv::Json val;
    val["MediaSource"] = ObjectStatistic<MediaSource>::count();
    val["MultiMediaSourceMuxer"] = ObjectStatistic<MultiMediaSourceMuxer>::count();
    /*
    val["TcpServer"] = ObjectStatistic<TcpServer>::count();
    val["TcpSession"] = ObjectStatistic<TcpSession>::count();
    val["UdpServer"] = ObjectStatistic<UdpServer>::count();
    val["UdpSession"] = ObjectStatistic<UdpSession>::count();
    val["TcpClient"] = ObjectStatistic<TcpClient>::count();
    val["Socket"] = ObjectStatistic<Socket>::count());
    */
    val["FrameImp"] = ObjectStatistic<FrameImp>::count();
    val["Frame"] = ObjectStatistic<Frame>::count();

    val["Buffer"] = ObjectStatistic<Buffer>::count();
    val["BufferRaw"] = ObjectStatistic<BufferRaw>::count();
    val["BufferLikeString"] = ObjectStatistic<BufferLikeString>::count();
    //val["BufferList"] = ObjectStatistic<BufferList>::count();

    val["RtpPacket"] = ObjectStatistic<RtpPacket>::count();
    val["RtmpPacket"] = ObjectStatistic<RtmpPacket>::count();
#ifdef ENABLE_MEM_DEBUG
    auto bytes = getTotalMemUsage();
    val["totalMemUsage"] = bytes;
    val["totalMemUsageMB"] = (int)(bytes / 1024 / 1024);
    val["totalMemBlock"] = getTotalMemBlock();
    static auto block_type_size = getBlockTypeSize();
    {
        int i = 0;
        string str;
        size_t last = 0;
        for (auto sz : block_type_size) {
            str.append(std::to_string(last) + "~" + std::to_string(sz) + ":" + std::to_string(getTotalMemBlockByType(i++)) + ";");
            last = sz;
        }
        str.pop_back();
        val["totalMemBlockTypeCount"] = str;
    }

    auto thread_size = EventPollerPool::Instance().getExecutorSize() + WorkThreadPool::Instance().getExecutorSize();
    std::shared_ptr<std::vector<Value> > thread_mem_info = std::make_shared<std::vector<Value> >(thread_size);

    std::shared_ptr<void> finished(nullptr, [thread_mem_info, cb, obj](void *) {
        for (auto &val : *thread_mem_info) {
            (*obj)["threadMem"].append(val);
        }
        //触发回调
        cb(*obj);
    });

    auto pos = 0;
    auto lam0 = [&](TaskExecutor &executor) {
        auto &val = (*thread_mem_info)[pos++];
        executor.async([finished, &val]() {
            auto bytes = getThisThreadMemUsage();
            val["threadName"] = getThreadName();
            val["threadMemUsage"] = bytes;
            val["threadMemUsageMB"] = (bytes / 1024 / 1024);
            val["threadMemBlock"] = getThisThreadMemBlock();
            {
                int i = 0;
                string str;
                size_t last = 0;
                for (auto sz : block_type_size) {
                    str.append(std::to_string(last) + "~" + std::to_string(sz) + ":" + std::to_string(getThisThreadMemBlockByType(i++)) + ";");
                    last = sz;
                }
                str.pop_back();
                val["threadMemBlockTypeCount"] = str;
            }
            });
    };
    auto lam1 = [lam0](const TaskExecutor::Ptr &executor) {
        lam0(*executor);
    };
    EventPollerPool::Instance().for_each(lam1);
    WorkThreadPool::Instance().for_each(lam1);
#else
    cb(val);
#endif
}

template<typename Args, typename First>
bool checkArgs(Args &args, const First &first) {
    return !args->GetString(first).empty();
}

template<typename Args, typename First, typename ...KeyTypes>
bool checkArgs(Args &args, const First &first, const KeyTypes &...keys) {
    return checkArgs(args, first) && checkArgs(args, keys...);
}

//检查http url中或body中或http header参数是否为空的宏
#define CHECK_ARGS(...)  \
    if(!checkArgs(req, ##__VA_ARGS__)){ \
        throw SockException(Err_refused, "缺少必要参数:" #__VA_ARGS__); \
    }

//检查http参数中是否附带secret密钥的宏，127.0.0.1的ip不检查密钥
#define CHECK_SECRET() \
    if(req->client_addr.ip != "127.0.0.1"){ \
        if(api_secret != req->GetString("secret")){ \
            throw SockException(Err_refused, "secret错误"); \
        } \
    }

void SockInfoToJson(hv::Json& val, SockInfo* info) {
    val["local_addr"] = info->localaddr();
    val["peer_addr"] = info->peeraddr();
    //val["identifier"] = info->getIdentifier();
}

hv::Json makeMediaSourceJson(MediaSource &media) {
    hv::Json item;
    item["schema"] = media.getSchema();
    item[VHOST_KEY] = media.getVhost();
    item["app"] = media.getApp();
    item["stream"] = media.getId();
    item["createStamp"] = media.getCreateStamp();
    item["aliveSecond"] = media.getAliveSecond();
    item["bytesSpeed"] = media.getBytesSpeed();
    item["readerCount"] = media.readerCount();
    item["totalReaderCount"] = media.totalReaderCount();
    item["originType"] = (int)media.getOriginType();
    item["originTypeStr"] = getOriginTypeString(media.getOriginType());
    item["originUrl"] = media.getOriginUrl();
    item["isRecordingMP4"] = media.isRecording(Recorder::type_mp4);
    item["isRecordingHLS"] = media.isRecording(Recorder::type_hls);
    auto originSock = media.getOriginSock();
    if (originSock) {
        SockInfoToJson(item["originSock"], originSock.get());
    }
    else {
        item["originSock"] = nullptr;
    }

    //getLossRate有线程安全问题；使用getMediaInfo接口才能获取丢包率；getMediaList接口将忽略丢包率
    auto current_thread = media.getOwnerPoller()->isInLoopThread();
    for (auto &track : media.getTracks(false)) {
        hv::Json obj;
        auto codec_type = track->getTrackType();
        obj["codec_id"] = track->getCodecId();
        obj["codec_id_name"] = track->getCodecName();
        obj["ready"] = track->ready();
        obj["codec_type"] = codec_type;
        if (current_thread) {
            obj["loss"] = media.getLossRate(codec_type);
        }
        switch (codec_type) {
        case TrackAudio: {
            auto audio_track = std::dynamic_pointer_cast<AudioTrack>(track);
            obj["sample_rate"] = audio_track->getAudioSampleRate();
            obj["channels"] = audio_track->getAudioChannel();
            obj["sample_bit"] = audio_track->getAudioSampleBit();
            break;
        }
        case TrackVideo: {
            auto video_track = std::dynamic_pointer_cast<VideoTrack>(track);
            obj["width"] = video_track->getVideoWidth();
            obj["height"] = video_track->getVideoHeight();
            obj["fps"] = round(video_track->getVideoFps());
            break;
        }
        default:
            break;
        }
        item["tracks"].push_back(obj);
    }
    return item;
}

#include "Rtmp/FlvMuxer.h"
class HttpFlvMuxer : public FlvMuxer, public std::enable_shared_from_this<HttpFlvMuxer>{
public:
    HttpFlvMuxer(HttpResponseWriterPtr writer) : _writer(writer) {
    }
    HttpFlvMuxer(WebSocketChannelPtr channel) : _channel(channel) {
    }
    using FlvMuxer::start;
protected:
    WebSocketChannelPtr _channel;
    HttpResponseWriterPtr _writer;
    // 写入二进制数据
    virtual void onWrite(const toolkit::Buffer::Ptr &data, bool flush) {
        if(_writer)
            _writer->WriteBody(data->data(), data->size());
        if(_channel)
            _channel->send(data->data(), data->size());
    }
    // stop或RtmpMediaSource Detach时回调
    virtual void onDetach() {
        if(_writer)
            _writer->close();
        if(_channel)
            _channel->close();
    }
    // 为了跨线程投递, 为啥不 enable_shared_from_this?
    virtual std::shared_ptr<FlvMuxer> getSharedPtr() {
        return shared_from_this();
    }
};

static std::string schema_from_stream(std::string& stream) {
    std::string schema;
    if (hv::endswith(stream, ".live.flv")) {
        schema = RTMP_SCHEMA;
        stream = stream.substr(0, stream.length() - 9);
    }
    else if (hv::endswith(stream, ".live.mp4")) {
        schema = FMP4_SCHEMA;
        stream = stream.substr(0, stream.length() - 9);
    }
    else if (hv::endswith(stream, ".live.ts")) {
        schema = TS_SCHEMA;
        stream = stream.substr(0, stream.length() - 8);
    }
    return schema;
}

hv::HttpService* getHttpService(){
    static hv::HttpService http;
    return &http;
}

void WebRtcServer::startHttp()
{
    static WebSocketService ws;
    ws.onopen = [](const WebSocketChannelPtr& channel, const HttpRequestPtr& req) {
        MediaInfo mi;
        mi.parse(req->url);
        std::string schema = mi._schema;
        if (schema.empty() || hv::startswith(schema, "ws")) {
            mi._schema = schema_from_stream(mi._streamid);
        }

        if (mi._app.empty() || mi._streamid.empty()) {
            //url不合法
            channel->close();
            return;
        }
        // 解析带上协议+参数完整的url
        // mi.parse(schema + "://" + req->host + req->path);
        MediaSource::findAsync(mi, channel, [schema, channel](const MediaSource::Ptr &src){
            std::weak_ptr<hv::WebSocketChannel> weak_self = channel;
            if (!src) {
                channel->close();
                return;
            }
            if (schema == RTMP_SCHEMA) {
                auto rtmp_src = std::dynamic_pointer_cast<RtmpMediaSource>(src);
                assert(rtmp_src);
                auto muxer = std::make_shared<HttpFlvMuxer>(channel);
                uint32_t start_pts = 0;//atoll(_parser.getUrlArgs()["starPts"].data());
                muxer->start(channel->getPoller(), rtmp_src, start_pts);
                channel->setContextPtr(muxer);
            }
            else if(schema == FMP4_SCHEMA) {
                auto fmp4_src = std::dynamic_pointer_cast<FMP4MediaSource>(src);
                assert(fmp4_src);
                //直播牺牲延时提升发送性能
                //setSocketFlags();

                // fmp4要先发initSegment
                channel->send(fmp4_src->getInitSegment(), WS_OPCODE_BINARY);
                fmp4_src->pause(false);
                auto reader = fmp4_src->getRing()->attach(channel->getPoller());
                reader->setDetachCB([weak_self]() {
                    if (auto strong_self = weak_self.lock()) {
                        strong_self->close();//SockException(Err_shutdown, "fmp4 ring buffer detached"));
                    }
                });
                reader->setReadCB([weak_self](const FMP4MediaSource::RingDataType &fmp4_list) {
                    if (auto strong_self = weak_self.lock()) {
                        for (auto pkt : *fmp4_list) {
                            strong_self->send(pkt->data(), pkt->size());
                        }
                    }
                });
                channel->setContextPtr(reader);
            }
            else if(schema == TS_SCHEMA) {
                auto ts_src = dynamic_pointer_cast<TSMediaSource>(src);
                assert(ts_src);
                //直播牺牲延时提升发送性能
                //setSocketFlags();

                ts_src->pause(false);
                auto reader = ts_src->getRing()->attach(channel->getPoller());
                reader->setDetachCB([weak_self]() {
                    if (auto strong_self = weak_self.lock()) {
                        strong_self->close();//SockException(Err_shutdown, "ts ring buffer detached"));
                    }
                });
                reader->setReadCB([weak_self](const TSMediaSource::RingDataType &ts_list) {
                    if (auto strong_self = weak_self.lock()) {
                        for (auto pkt : *ts_list) {
                            strong_self->send(pkt->data(), pkt->size());
                        }
                    }
                });
                channel->setContextPtr(reader);
            }
        });        
    };
    ws.onmessage = [](const WebSocketChannelPtr& channel, const std::string& msg) {
        // MyContext* ctx = channel->getContext<MyContext>();
        // ctx->handleMessage(msg);
    };
    ws.onclose = [](const WebSocketChannelPtr& channel) {
        channel->setContextPtr(nullptr);
    };

    auto http = getHttpService();
    http->document_root = mINI::Instance()[Http::kRootPath];
    InfoL << "www root:" << http->document_root;
    // http->error_page = mINI::Instance()[Http::kNotFound];
    _http.port = mINI::Instance()[RTC::kHttpPort];
    _http.https_port = mINI::Instance()[RTC::kHttpsPort];
    if (_http.https_port) {
        hssl_ctx_init_param_t param;
        memset(&param, 0, sizeof(param));
        auto key = mINI::Instance()[RTC::kKeyFile];
        auto cert = mINI::Instance()[RTC::kCertFile];
        param.crt_file = cert.c_str();
        param.key_file = key.c_str();
        param.endpoint = HSSL_SERVER;
        if (hssl_ctx_init(&param) == NULL) {
            fprintf(stderr, "hssl_ctx_init failed!\n");
        }
    }
    setupRestApi(*http);

    _http.service = http;
    _http.ws = &ws;
    _http.share_pools = EventLoopThreadPool::Instance().get();
    websocket_server_run(&_http, 0);
}

bool delStreamProxy(const std::string& key){
    AutoLock lck(s_proxyMapMtx);
    return (s_proxyMap.erase(key) == 1);
}

void addStreamProxy(const string& vhost, const string& app, const string& stream,
    const string& url, int retry_count, const ProtocolOption& option, int rtp_type, float timeout_sec,
    const std::function<void(const SockException& ex, const string& key)>& cb) {
    auto key = getProxyKey(vhost, app, stream);
    AutoLock lck(s_proxyMapMtx);
    if (s_proxyMap.find(key) != s_proxyMap.end()) {
        //已经在拉流了
        cb(SockException(Err_success), key);
        return;
    }
    //添加拉流代理
    auto player = std::make_shared<PlayerProxy>(vhost, app, stream, option, retry_count ? retry_count : -1);
    s_proxyMap[key] = player;

    //指定RTP over TCP(播放rtsp时有效)
    (*player)[Client::kRtpType] = rtp_type;

    if (timeout_sec > 0.1) {
        //播放握手超时时间
        (*player)[Client::kTimeoutMS] = timeout_sec * 1000;
    }

    //开始播放，如果播放失败或者播放中止，将会自动重试若干次，默认一直重试
    player->setPlayCallbackOnce([cb, key](const SockException& ex) {
        if (ex) {
            AutoLock lck(s_proxyMapMtx);
            s_proxyMap.erase(key);
        }
        cb(ex, key);
    });

    //被主动关闭拉流
    player->setOnClose([key](const SockException& ex) {
        AutoLock lck(s_proxyMapMtx);
        s_proxyMap.erase(key);
    });
    player->play(url);
};

void WebRtcServer::setupRestApi(HttpService &http)
{
    GET_CONFIG(std::string, api_secret, "api.secret");
    http.POST("/echo", [](const HttpContextPtr& ctx) {
        return ctx->send(ctx->body(), ctx->type());
    });

    http.POST("/index/api/webrtc", [this](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        onRtcOfferReq(req, writer);
    });

    http.GET("/index/api/getStatistic", [this](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        getStatisticJson([writer](const hv::Json &data) mutable {
            HttpResponse resp;
            resp.Json(data);
            writer->WriteResponse(&resp);
        });
    });

    http.GET("/paths", [&http](HttpRequest* req, HttpResponse* resp) {
        return resp->Json(http.Paths());
    });

    http.Any("/index/api/getServerConfig", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        hv::Json val;
        for (auto &pr : mINI::Instance()) {
            val[pr.first] = pr.second;
        }
        return resp->Json(val);
    });

    //设置服务器配置
    //测试url(比如关闭http api调试) http://127.0.0.1/index/api/setServerConfig?api.apiDebug=0
    //你也可以通过http post方式传参，可以通过application/x-www-form-urlencoded或application/json方式传参
    http.Any("/index/api/setServerConfig", [](HttpRequest* req, HttpResponse* resp) {
        CHECK_SECRET();
        auto &ini = mINI::Instance();
        int changed = 0;
        //@todo allArgs.getArgs()
        for (auto &pr : req->kv) {
            if (ini.find(pr.first) == ini.end()) {
#if 1
                //没有这个key
                continue;
#else
                // 新增配置选项,为了动态添加多个ffmpeg cmd 模板
                ini[pr.first] = pr.second;
                // 防止changed变化
                continue;
#endif
            }
            if (ini[pr.first] == pr.second) {
                continue;
            }
            ini[pr.first] = pr.second;
            //替换成功
            ++changed;
        }
        if (changed > 0) {
            NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastReloadConfig);
            // 保存配置文件
            ini.dumpFile(g_ini_file);
        }
        resp->Set("changed", changed);
        return HTTP_STATUS_OK;
    });

    http.GET("/index/api/getMediaList", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        hv::Json val;
        //获取所有MediaSource列表
        MediaSource::for_each_media([&](const MediaSource::Ptr &media) {
            val["data"].push_back(makeMediaSourceJson(*media));
        }, req->GetString("schema"), req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));
        return resp->Json(val);
    });

    //测试url http://127.0.0.1/index/api/isMediaOnline?schema=rtsp&vhost=__defaultVhost__&app=live&stream=obs
    http.GET("/index/api/isMediaOnline", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("schema", "vhost", "app", "stream");
        hv::Json val;
        val["online"] = (bool)(MediaSource::find(req->GetString("schema"), req->GetString("vhost"), req->GetString("app"), req->GetString("stream")));
        return resp->Json(val);
    });

    //测试url http://127.0.0.1/index/api/getMediaInfo?schema=rtsp&vhost=__defaultVhost__&app=live&stream=obs
    http.Any("/index/api/getMediaInfo", [this](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("schema", "vhost", "app", "stream");
        auto src = MediaSource::find(req->GetString("schema"), req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));
        if (!src) {
            throw SockException(Err_other, "can not find the stream");
        }
        src->getOwnerPoller()->async([=]() mutable {
            auto val = makeMediaSourceJson(*src);
            val["code"] = 0;
            writer->End(val.dump());
        });
    });

    //主动关断流，包括关断拉流、推流
    //测试url http://127.0.0.1/index/api/close_stream?schema=rtsp&vhost=__defaultVhost__&app=live&stream=obs&force=1
    http.Any("/index/api/close_stream", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("schema", "vhost", "app", "stream");
        //踢掉推流器
        auto src = MediaSource::find(req->GetString("schema"), req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));
        if (!src) {
            throw SockException(Err_other, "can not find the stream");
        }

        bool force = req->Get<bool>("force");
        src->getOwnerPoller()->async([=]() mutable {
            bool flag = src->close(force);
            hv::Json val;
            val["result"] = flag ? 0 : -1;
            val["msg"] = flag ? "success" : "close failed";
            val["code"] = flag ? 0 : -1;
            writer->End(val.dump());
        });
    });

    //批量主动关断流，包括关断拉流、推流
    //测试url http://127.0.0.1/index/api/close_streams?schema=rtsp&vhost=__defaultVhost__&app=live&stream=obs&force=1
    http.Any("/index/api/close_streams", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        //筛选命中个数
        int count_hit = 0;
        int count_closed = 0;
        std::list<MediaSource::Ptr> media_list;
        MediaSource::for_each_media([&](const MediaSource::Ptr &media) {
            ++count_hit;
            media_list.emplace_back(media);
            }, req->GetString("schema"), req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));

        bool force = req->GetBool("force");
        for (auto &media : media_list) {
            if (media->close(force)) {
                ++count_closed;
            }
        }
        hv::Json val;
        val["count_hit"] = count_hit;
        val["count_closed"] = count_closed;
        resp->Json(val);
        return HTTP_STATUS_OK;
    });

    static auto addStreamPusherProxy = [](const string &schema,
        const string &vhost,
        const string &app,
        const string &stream,
        const string &url,
        int retry_count,
        int rtp_type,
        float timeout_sec,
        const std::function<void(const SockException &ex, const string &key)> &cb) {
            auto key = getPusherKey(schema, vhost, app, stream, url);
            auto src = MediaSource::find(schema, vhost, app, stream);
            if (!src) {
                cb(SockException(Err_other, "can not find the source stream"), key);
                return;
            }

            AutoLock lck(s_proxyPusherMapMtx);
            if (s_proxyPusherMap.find(key) != s_proxyPusherMap.end()) {
                //已经在推流了
                cb(SockException(Err_success), key);
                return;
            }

            //添加推流代理
            PusherProxy::Ptr pusher(new PusherProxy(src, retry_count ? retry_count : -1));
            s_proxyPusherMap[key] = pusher;

            //指定RTP over TCP(播放rtsp时有效)
            (*pusher)[Client::kRtpType] = rtp_type;

            if (timeout_sec > 0.1) {
                //推流握手超时时间
                (*pusher)[Client::kTimeoutMS] = timeout_sec * 1000;
            }

            //开始推流，如果推流失败或者推流中止，将会自动重试若干次，默认一直重试
            pusher->setPushCallbackOnce([cb, key, url](const SockException &ex) {
                if (ex) {
                    WarnL << "Push " << url << " failed, key: " << key << ", err: " << ex.what();
                    AutoLock lck(s_proxyPusherMapMtx);
                    s_proxyPusherMap.erase(key);
                }
                cb(ex, key);
            });

            //被主动关闭推流
            pusher->setOnClose([key, url](const SockException &ex) {
                WarnL << "Push " << url << " failed, key: " << key << ", err: " << ex.what();
                AutoLock lck(s_proxyPusherMapMtx);
                s_proxyPusherMap.erase(key);
            });
            pusher->publish(url);
    };

    //动态添加rtsp/rtmp推流代理
    //测试url http://127.0.0.1/index/api/addStreamPusherProxy?schema=rtmp&vhost=__defaultVhost__&app=proxy&stream=0&dst_url=rtmp://127.0.0.1/live/obs
    http.Any("/index/api/addStreamPusherProxy", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("schema", "vhost", "app", "stream", "dst_url");
        auto dst_url = req->GetString("dst_url");
        addStreamPusherProxy(req->GetString("schema"),
            req->GetString("vhost"),
            req->GetString("app"),
            req->GetString("stream"),
            req->GetString("dst_url"),
            req->GetInt("retry_count"),
            req->GetInt("rtp_type"),
            req->GetInt("timeout_sec"),
            [writer, dst_url](const SockException &ex, const std::string &key) mutable {
                hv::Json val;
                if (ex) {
                    val["code"] = -1;
                    val["msg"] = ex.what();
                }
                else {
                    val["data"]["key"] = key;
                    InfoL << "Publish success, please play with player:" << dst_url;
                }
                writer->response->Json(val);
                writer->End();
            });
        });

    //关闭推流代理
    //测试url http://127.0.0.1/index/api/delStreamPusherProxy?key=__defaultVhost__/proxy/0
    http.Any("/index/api/delStreamPusherProxy", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("key");
        AutoLock lck(s_proxyPusherMapMtx);
        hv::Json val;
        val["data"]["flag"] = s_proxyPusherMap.erase(req->GetString("key")) == 1;
        resp->Json(val);
        return HTTP_STATUS_OK;
    });

    //动态添加rtsp/rtmp拉流代理
    //测试url http://127.0.0.1/index/api/addStreamProxy?vhost=__defaultVhost__&app=proxy&enable_rtsp=1&enable_rtmp=1&stream=0&url=rtmp://127.0.0.1/live/obs
    http.Any("/index/api/addStreamProxy", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("vhost", "app", "stream", "url");

        ProtocolOption option;
        option.enable_hls = req->GetBool("enable_hls");
        option.enable_mp4 = req->GetBool("enable_mp4");
        option.enable_rtsp = req->GetBool("enable_rtsp");
        option.enable_rtmp = req->GetBool("enable_rtmp");
        option.enable_ts = req->GetBool("enable_ts");
        option.enable_fmp4 = req->GetBool("enable_fmp4");
        option.enable_audio = req->GetBool("enable_audio");
        option.add_mute_audio = req->GetBool("add_mute_audio");
        option.mp4_save_path = req->GetString("mp4_save_path");
        option.mp4_max_second = req->GetInt("mp4_max_second");
        option.hls_save_path = req->GetString("hls_save_path");

        addStreamProxy(req->GetString("vhost"),
            req->GetString("app"),
            req->GetString("stream"),
            req->GetString("url"),
            req->GetInt("retry_count"),
            option,
            req->GetInt("rtp_type"),
            req->GetInt("timeout_sec"),
            [writer](const SockException &ex, const string &key) mutable {
                hv::Json val;
                if (ex) {
                    val["code"] = -1;
                    val["msg"] = ex.what();
                }
                else {
                    val["data"]["key"] = key;
                }
                writer->response->Json(val);
                writer->End();
            });
        });

    //关闭拉流代理
    //测试url http://127.0.0.1/index/api/delStreamProxy?key=__defaultVhost__/proxy/0
    http.Any("/index/api/delStreamProxy", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("key");
        AutoLock lck(s_proxyMapMtx);
        hv::Json val;
        val["data"]["flag"] = s_proxyMap.erase(req->GetString("key")) == 1;
        resp->Json(val);
        return HTTP_STATUS_OK;
    });

#if defined(ENABLE_RTPPROXY)
    http.Any("/index/api/getRtpInfo", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("stream_id");

        auto process = RtpSelector::Instance().getProcess(req->GetString("stream_id"), false);
        if (!process) {
            resp->Set("exist", false);
        }
        else {
            hv::Json val;
            val["exist"] = true;
            //SockInfoToJson(val, process.get());
            resp->Json(val);
        }
        return HTTP_STATUS_OK;
    });

    http.Any("/index/api/openRtpServer", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("port", "enable_tcp", "stream_id");
        auto stream_id = req->GetString("stream_id");
        auto port = openRtpServer(req->GetInt("port"), stream_id, req->GetBool("enable_tcp"), "::",
            req->GetBool("re_use_port"), req->GetInt("ssrc"));
        if (port == 0) {
            resp->Set("error", "该stream_id已存在");
        }
        else {
            //回复json
            resp->Set("port", port);
        }
        return HTTP_STATUS_OK;
    });

    http.Any("/index/api/closeRtpServer", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("stream_id");

        bool ret = closeRtpServer(req->GetString("stream_id"));
        resp->Set("hit", ret);
        return HTTP_STATUS_OK;
    });

    http.Any("/index/api/listRtpServer", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();

        hv::Json val;
        AutoLock lck(s_rtpServerMapMtx);
        for (auto &pr : s_rtpServerMap) {
            hv::Json obj;
            obj["stream_id"] = pr.first;
            obj["port"] = pr.second->getPort();
            val.push_back(obj);
        }
        resp->Set("data", val);
        return HTTP_STATUS_OK;
    });

    http.Any("/index/api/startSendRtp", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("vhost", "app", "stream", "ssrc", "dst_url", "dst_port", "is_udp");

        auto src = MediaSource::find(req->GetString("vhost"), req->GetString("app"), req->GetString("stream"), req->GetBool("from_mp4"));
        if (!src) {
            writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
            writer->End("can not find the source stream");
            return;
        }

        MediaSourceEvent::SendRtpArgs args;
        args.passive = false;
        args.dst_url = req->GetString("dst_url");
        args.dst_port = req->GetInt("dst_port");
        args.ssrc = req->GetInt("ssrc");
        args.is_udp = req->GetBool("is_udp");
        args.src_port = req->GetInt("src_port");
        args.pt = req->GetInt("pt", 96);
        args.use_ps = req->GetBool("use_ps", true);
        args.only_audio = req->GetBool("only_audio", false);
        TraceL << "startSendRtp, pt " << int(args.pt) << " ps " << args.use_ps << " audio " << args.only_audio;

        src->getOwnerPoller()->async([=]() mutable {
            src->startSendRtp(args, [writer](uint16_t local_port, const SockException &ex) mutable {
                hv::Json val;
                if (ex) {
                    val["code"] = -1;
                    val["msg"] = ex.what();
                }
                val["local_port"] = local_port;
                writer->response->Json(val);
                writer->End();
            });
        });
    });

    http.Any("/index/api/startSendRtpPassive", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("vhost", "app", "stream", "ssrc");

        auto src = MediaSource::find(req->GetString("vhost"), req->GetString("app"), req->GetString("stream"), req->GetInt("from_mp4"));
        if (!src) {
            writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
            writer->End("can not find the source stream");
            return;
        }

        MediaSourceEvent::SendRtpArgs args;
        args.passive = true;
        args.ssrc = req->GetInt("ssrc");
        args.is_udp = false;
        args.src_port = req->GetInt("src_port");
        args.pt = req->GetInt("pt", 96);
        args.use_ps = req->GetBool("use_ps", true);
        args.only_audio = req->GetBool("only_audio");
        TraceL << "startSendRtpPassive, pt " << int(args.pt) << " ps " << args.use_ps << " audio " << args.only_audio;

        src->getOwnerPoller()->async([=]() mutable {
            src->startSendRtp(args, [writer](uint16_t local_port, const SockException &ex) mutable {
                hv::Json val;
                if (ex) {
                    val["code"] = -1;
                    val["msg"] = ex.what();
                }
                val["local_port"] = local_port;
                writer->response->Json(val);
                writer->End();
            });
        });
    });

    http.Any("/index/api/stopSendRtp", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("vhost", "app", "stream");

        auto src = MediaSource::find(req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));
        if (!src) {
            writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
            writer->End("can not find the source stream");
        }

        src->getOwnerPoller()->async([=]() mutable {
            // ssrc如果为空，关闭全部
            if (!src->stopSendRtp(req->GetString("ssrc"))) {
                hv::Json val;
                val["code"] = -1;
                val["msg"] = "stopSendRtp failed";
                writer->response->Json(val);
                writer->End();
                return;
            }
            writer->End();
        });
    });

    http.Any("/index/api/pauseRtpCheck", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("stream_id");
        //只是暂停流的检查，流媒体服务器做为流负载服务，收流就转发，RTSP/RTMP有自己暂停协议
        auto rtp_process = RtpSelector::Instance().getProcess(req->GetString("stream_id"), false);
        if (rtp_process) {
            rtp_process->setStopCheckRtp(true);
        }
        else {
            resp->Set("code", 404);
        }
        return HTTP_STATUS_OK;
    });

    http.Any("/index/api/resumeRtpCheck", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("stream_id");
        auto rtp_process = RtpSelector::Instance().getProcess(req->GetString("stream_id"), false);
        if (rtp_process) {
            rtp_process->setStopCheckRtp(false);
        }
        else {
            resp->Set("code", 404);
        }
        return HTTP_STATUS_OK;
    });

#endif//ENABLE_RTPPROXY

    // 开始录制hls或MP4
    http.Any("/index/api/startRecord", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("type", "vhost", "app", "stream");

        auto src = MediaSource::find(req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));
        if (!src) {
            writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
            writer->End("can not find the stream");
            return;
        }

        writer->WriteStatus(HTTP_STATUS_OK);
        src->getOwnerPoller()->async([=]() mutable {
            auto result = src->setupRecord((Recorder::type)req->GetInt("type"), true, req->GetString("customized_path"), req->GetInt("max_second"));
            auto resp = writer->response;
            resp->Set("result", result);
            resp->Set("code", result ? 0 : -1);
            resp->Set("msg", result ? "success" : "start record failed");
            writer->End();
        });
    });

    //设置录像流播放速度
    http.Any("/index/api/setRecordSpeed", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("schema", "vhost", "app", "stream", "speed");
        auto src = MediaSource::find(req->GetString("schema"), req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));
        if (!src) {
            writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
            writer->End("can not find the stream");
            return;
        }

        writer->WriteStatus(HTTP_STATUS_OK);
        auto speed = req->GetFloat("speed");
        src->getOwnerPoller()->async([=]() mutable {
            bool flag = src->speed(speed);
            auto resp = writer->response;
            resp->Set("result", flag ? 0 : -1);
            resp->Set("code", flag ? 0 : -1);
            resp->Set("msg", flag ? "success" : "set failed");
            writer->End();
        });
    });

    http.Any("/index/api/seekRecordStamp", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("schema", "vhost", "app", "stream", "stamp");
        auto src = MediaSource::find(req->GetString("schema"), req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));
        if (!src) {
            writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
            writer->End("can not find the stream");
            return;
        }

        writer->WriteStatus(HTTP_STATUS_OK);
        auto stamp = req->GetInt("stamp");
        src->getOwnerPoller()->async([=]() mutable {
            bool flag = src->seekTo(stamp);
            auto resp = writer->response;
            resp->Set("result", flag ? 0 : -1);
            resp->Set("code", flag ? 0 : -1);
            resp->Set("msg", flag ? "success" : "set failed");
            writer->End();
        });
    });

    // 停止录制hls或MP4
    http.Any("/index/api/stopRecord", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("type", "vhost", "app", "stream");

        auto src = MediaSource::find(req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));
        if (!src) {
            writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
            writer->End("can not find the stream");
        }

        writer->WriteStatus(HTTP_STATUS_OK);
        auto type = (Recorder::type)req->GetInt("type");
        src->getOwnerPoller()->async([=]() mutable {
            auto result = src->setupRecord(type, false, "", 0);
            auto resp = writer->response;
            resp->Set("result", result);
            resp->Set("code", result ? 0 : -1);
            resp->Set("msg", result ? "success" : "stop record failed");
            writer->End();
        });
    });

    // 获取hls或MP4录制状态
    http.Any("/index/api/isRecording", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("type", "vhost", "app", "stream");

        auto src = MediaSource::find(req->GetString("vhost"), req->GetString("app"), req->GetString("stream"));
        if (!src) {
            writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
            writer->End("can not find the stream");
        }

        writer->WriteStatus(HTTP_STATUS_OK);
        auto type = (Recorder::type)req->GetInt("type");
        src->getOwnerPoller()->async([=]() mutable {
            writer->response->Set("status", src->isRecording(type));
            writer->End();
        });
    });

    // 删除录像文件夹
    // http://127.0.0.1/index/api/deleteRecordDirectroy?vhost=__defaultVhost__&app=live&stream=ss&period=2020-01-01
    http.Any("/index/api/deleteRecordDirectory", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("vhost", "app", "stream");
        auto record_path = Recorder::getRecordPath(Recorder::type_mp4,
            req->GetString("vhost"), req->GetString("app"), req->GetString("stream"), req->GetString("customized_path"));
        auto period = req->GetString("period");
        record_path = record_path + period + "/";
        int result = File::delete_file(record_path.data());
        if (result) {
            // 不等于0时代表失败
            record_path = "delete error";
        }
        auto val = writer->response;
        val->Set("path", record_path);
        val->Set("code", result);
        writer->End();
    });

    //获取录像文件夹列表或mp4文件列表
    //http://127.0.0.1/index/api/getMp4RecordFile?vhost=__defaultVhost__&app=live&stream=ss&period=2020-01
    http.Any("/index/api/getMp4RecordFile", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        CHECK_SECRET();
        CHECK_ARGS("vhost", "app", "stream");
        auto record_path = Recorder::getRecordPath(Recorder::type_mp4,
            req->GetString("vhost"), req->GetString("app"), req->GetString("stream"), req->GetString("customized_path"));
        auto period = req->GetString("period");

        //判断是获取mp4文件列表还是获取文件夹列表
        bool search_mp4 = period.size() == sizeof("2020-02-01") - 1;
        if (search_mp4) {
            record_path = record_path + period + "/";
        }

        hv::Json paths;
        //这是筛选日期，获取文件夹列表
        File::scanDir(record_path, [&](const string &path, bool isDir) {
            auto pos = path.rfind('/');
            if (pos != string::npos) {
                string relative_path = path.substr(pos + 1);
                if (search_mp4) {
                    if (!isDir) {
                        //我们只收集mp4文件，对文件夹不感兴趣
                        paths.push_back(relative_path);
                    }
                }
                else if (isDir && relative_path.find(period) == 0) {
                    //匹配到对应日期的文件夹
                    paths.push_back(relative_path);
                }
            }
            return true;
        }, false);
        hv::Json val;
        val["data"]["rootPath"] = record_path;
        val["data"]["paths"] = paths;
        writer->response->Json(val);
        writer->End();
    });


    // /:field/ => req->query_params["field"]
    http.GET("/:app/:stream", [this](const HttpContextPtr& context) -> int {
        auto req = context->request;
        auto writer = context->writer;
        InfoL << "stream: " << req->url;
        MediaInfo mi;
        //mi.parse(req->url);
        mi._host = req->host;
        mi._app = req->query_params["app"];
        mi._streamid = req->query_params["stream"];
        std::string stream = mi._streamid;

        auto schema = req->query_params["schema"];
        if (schema.empty()) {
            schema = schema_from_stream(mi._streamid);
        }
        mi._schema = schema;
        if (mi._app.empty() || mi._streamid.empty() || schema.empty()) {
            return -1;
            //url不合法
            writer->End("stream url error");
            return HTTP_STATUS_NOT_FOUND;
        }

        // 解析带上协议+参数完整的url
        // mi.parse(schema + "://" + req->host + req->path);
        MediaSource::findAsync(mi, writer, [schema, stream, writer](const MediaSource::Ptr &src) {
            std::weak_ptr<hv::HttpResponseWriter> weak_self = writer;
            if (!src) {
                writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
                writer->End("stream not found");
                return;
            }
            writer->response->SetContentTypeByFilename(stream.c_str());
            writer->EndHeaders();

            if (schema == RTMP_SCHEMA) {
                auto rtmp_src = std::dynamic_pointer_cast<RtmpMediaSource>(src);
                assert(rtmp_src);
                auto muxer = std::make_shared<HttpFlvMuxer>(writer);
                uint32_t start_pts = 0;//atoll(_parser.getUrlArgs()["starPts"].data());
                muxer->start(writer->getPoller(), rtmp_src, start_pts);
                writer->setContextPtr(muxer);
            }
            else if (schema == FMP4_SCHEMA) {
                auto fmp4_src = std::dynamic_pointer_cast<FMP4MediaSource>(src);
                assert(fmp4_src);
                //直播牺牲延时提升发送性能
                //setSocketFlags();

                // fmp4要先发initSegment
                writer->WriteBody(fmp4_src->getInitSegment());
                fmp4_src->pause(false);
                auto reader = fmp4_src->getRing()->attach(writer->getPoller());
                reader->setDetachCB([weak_self]() {
                    if (auto strong_self = weak_self.lock()) {
                        strong_self->close();//SockException(Err_shutdown, "fmp4 ring buffer detached"));
                    }
                });
                reader->setReadCB([weak_self](const FMP4MediaSource::RingDataType &fmp4_list) {
                    if (auto strong_self = weak_self.lock()) {
                        for (auto pkt : *fmp4_list) {
                            strong_self->WriteBody(pkt->data(), pkt->size());
                        }
                    }
                });
                writer->setContextPtr(reader);
            }
            else if (schema == TS_SCHEMA) {
                auto ts_src = std::dynamic_pointer_cast<TSMediaSource>(src);
                assert(ts_src);
                //直播牺牲延时提升发送性能
                //setSocketFlags();

                ts_src->pause(false);
                auto reader = ts_src->getRing()->attach(writer->getPoller());
                reader->setDetachCB([weak_self]() {
                    if (auto strong_self = weak_self.lock()) {
                        strong_self->close();//SockException(Err_shutdown, "ts ring buffer detached"));
                    }
                });
                reader->setReadCB([weak_self](const TSMediaSource::RingDataType &ts_list) {
                    if (auto strong_self = weak_self.lock()) {
                        for (auto pkt : *ts_list) {
                            strong_self->WriteBody(pkt->data(), pkt->size());
                        }
                    }
                });
                writer->setContextPtr(reader);
            }
        });
        return 0;
    });
}

void WebRtcServer::onRtcOfferReq(const HttpRequestPtr& req, const HttpResponseWriterPtr& writer)
{
    auto type = req->GetParam("type");
    auto offer = req->Body();
    CHECK(!offer.empty(), "http body(webrtc offer sdp) is empty");
    WebRtcArgs args = req->query_params;
    if (args.count("app") && args.count("stream")) {
        args["url"] = StrPrinter << RTC_SCHEMA << "://" << req->GetHeader("Host", DEFAULT_VHOST) << "/" << args["app"] << "/" << args["stream"];
            //<< "?" << args.getParser().Params() + "&session=" + _session_id;
    }
    try {
        getAnswerSdp(writer, type, args, [writer, offer](const WebRtcInterface &exchanger) mutable {
            HttpResponse resp;
            //设置返回类型
            resp.SetHeader("Content-Type", "application/json");
            //设置跨域
            resp.SetHeader("Access-Control-Allow-Origin", "*");

            try {
                resp.Set("code", 0);
                resp.Set("sdp", const_cast<WebRtcInterface &>(exchanger).getAnswerSdp(offer));
                resp.Set("id", exchanger.getIdentifier());
                resp.Set("type", "answer");
            }
            catch (std::exception &ex) {
                WarnL << "onRtcOfferReq exception: " << ex.what();
                resp.Set("code", "exception");
                resp.Set("msg", ex.what());
            }
            writer->WriteResponse(&resp);
        });
    }
    catch(std::exception e){
        HttpResponse resp;
        resp.Set("code", "exception");
        resp.Set("msg", e.what());
        WarnL << "onRtcOfferReq type unsupported: " << type;
        writer->WriteResponse(&resp);
    }
}

void WebRtcServer::getAnswerSdp(hv::SocketChannelPtr sender, const std::string &type, const WebRtcArgs &args, const onCreateRtc &cb)
{
    std::lock_guard<std::mutex> lck(_mtx_creator);
    auto it = _map_creator.find(type);
    if (it == _map_creator.end()) {
        cb(WebRtcException(SockException(Err_other, "the type can not supported")));
        return;
    }
    it->second(sender, args, cb);
}

std::shared_ptr<WebRtcServer::RtcServer> WebRtcServer::newRtcServer(int port)
{
    auto rtc = std::make_shared<RtcServer>();
    int listenfd = rtc->createsocket(port);
    if (listenfd < 0)
        return nullptr;

    LOGI("listen rtc on %d, listenfd=%d ...\n", port, listenfd);

    rtc->onNewClient = [rtc](hio_t* io, hv::Buffer* data) {
        // @todo select loop with data or peerAddr
        auto user_name = WebRtcSession::getUserName(data->data(), data->size());
        if (user_name.empty()) {
            return;
        }
        if (auto item = WebRtcServer::Instance().getItem(user_name))
            rtc->accept(io, item->getPoller());
    };
    rtc->onMessage = [](const std::shared_ptr<WebRtcSession>& session, hv::Buffer* buf) {
        try {
            session->onRecv(buf);
        }
        catch (SockException& ex) {
            session->shutdown(ex);
        }
        catch (exception& ex) {
            session->shutdown(SockException(Err_shutdown, ex.what()));
        }
    };
    rtc->setLoadBalance(LB_LeastConnections);
    rtc->start();
    return rtc;
}

std::shared_ptr<WebRtcServer::RtcTcpServer> WebRtcServer::newRtcTcpServer(int port)
{
    auto rtc = std::make_shared<RtcTcpServer>();
    int listenfd = rtc->createsocket(port);
    if (listenfd < 0) {
        return nullptr;
    }
    LOGI("listen rtc on tcp %d", port);
    rtc->onConnection = [](const std::shared_ptr<WebRtcSession>& session) {
        if (session->isConnected()) {
        }
        else {
            session->onError(SockException(Err_eof, "socket closed"));
        }
    };
    rtc->onMessage = [](const std::shared_ptr<WebRtcSession>& session, hv::Buffer* buf) {
        try {
            session->onRecv(buf);
        }
        catch (SockException &ex) {
            session->shutdown(ex);
        }
        catch (exception &ex) {
            session->shutdown(SockException(Err_shutdown, ex.what()));
        }
    };
    rtc->setLoadBalance(LB_LeastConnections);
    rtc->start();
    return rtc;
}

std::shared_ptr<WebRtcServer::SrtServer> WebRtcServer::newSrtServer(int port)
{
#ifdef ENABLE_SRT    
    auto srt = std::make_shared<SrtServer>();
    int listenfd = srt->createsocket(port);
    if (listenfd < 0) {
        return nullptr;
    }
    LOGI("listen srt on %d, listenfd=%d ...\n", port, listenfd);

    srt->onNewClient = [srt](hio_t* io, hv::Buffer* data) {
        // @todo select loop with data or peerAddr
        auto new_poller = SRT::SrtSession::queryPoller((uint8_t*)data->data(), data->size());
        srt->accept(io, new_poller);
    };
    srt->onMessage = [](const std::shared_ptr<SRT::SrtSession>& session, hv::Buffer* data) {
        try {
            session->onRecv((uint8_t*)data->data(), data->size());
        }
        catch (SockException& ex) {
            session->shutdown(ex);
        }
        catch (exception& ex) {
            session->shutdown(SockException(Err_shutdown, ex.what()));
        }
    };
    srt->setLoadBalance(LB_LeastConnections);
    srt->start();
    return srt;
#else 
    return nullptr;
#endif    
}

std::shared_ptr<WebRtcServer::RtmpServer> WebRtcServer::newRtmpServer(int port)
{
    auto rtmp = std::make_shared<RtmpServer>();
    int listenfd = rtmp->createsocket(port);
    if (listenfd < 0) {
        return nullptr;
    }
    LOGI("listen rtmp on %d", port);
    rtmp->onConnection = [](const std::shared_ptr<RtmpSession>& session) {
        if (session->isConnected()) {
        }
        else {
            session->onError(SockException(Err_eof, "socket closed"));
        }
    };
    rtmp->onMessage = [](const std::shared_ptr<RtmpSession>& session, hv::Buffer* buf) {
        try {
            session->onRecv((char*)buf->data(), buf->size());
        }
        catch (SockException &ex) {
            session->shutdown(ex);
        }
        catch (exception &ex) {
            session->shutdown(SockException(Err_shutdown, ex.what()));
        }
    };
    rtmp->setLoadBalance(LB_LeastConnections);
    rtmp->start();
    return rtmp;
}

std::shared_ptr<WebRtcServer::RtspServer> WebRtcServer::newRtspServer(int port)
{
    auto rtsp = std::make_shared<RtspServer>();
    int listenfd = rtsp->createsocket(port);
    if (listenfd < 0) {
        return nullptr;
    }

    LOGI("listen rtsp on %d", port);
    rtsp->onConnection = [](const std::shared_ptr<RtspSession>& session) {
        if (session->isConnected()) {
        }
        else {
            session->onError(SockException(Err_eof, "socket closed"));
        }
    };
    rtsp->onMessage = [](const std::shared_ptr<RtspSession>& session, hv::Buffer* buf) {
        try {
            auto buffer = BufferRaw::create();
            buffer->assign((char*)buf->data(), buf->size());
            session->onRecv(buffer);
        }
        catch (SockException &ex) {
            session->shutdown(ex);
        }
        catch (exception &ex) {
            session->shutdown(SockException(Err_shutdown, ex.what()));
        }
    };
    rtsp->setLoadBalance(LB_LeastConnections);
    rtsp->start();
    return rtsp;
}

std::shared_ptr<WebRtcServer::ShellServer> WebRtcServer::newShellServer(int port)
{
    auto ret = std::make_shared<ShellServer>();
    int listenfd = ret->createsocket(port);
    if (listenfd < 0) {
        return nullptr;
    }
    LOGI("listen shell on %d", port);
    ret->onMessage = [](const std::shared_ptr<ShellSession>& session, hv::Buffer* buf) {
        try {
            session->onRecv((char*)buf->data(), buf->size());
        }
        catch (SockException& ex) {
            session->shutdown(ex);
        }
        catch (exception& ex) {
            session->shutdown(SockException(Err_shutdown, ex.what()));
        }
    };
    return ret;
}
