/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <iostream>

#include "Util/logger.h"
#include "Util/SSLBox.h"
#include "Util/onceToken.h"
#include "Network/TcpServer.h"
#include "Poller/EventPoller.h"

#include "Common/config.h"
#include "Rtsp/RtspSession.h"
#include "Rtmp/RtmpSession.h"
#include "Http/WebSocketSession.h"

#include "SDLCapture.h"
#include "Common/Device.h"

using namespace std;
using namespace toolkit;
using namespace mediakit;

namespace mediakit {
// //////////HTTP配置///////////
#define THEAD_COUNT "general.threads"
namespace Camera {
#define CAMERA_FIELD "camera."
const string kWidth = CAMERA_FIELD"width";
const string kHeight = CAMERA_FIELD"height";
const string kFramerate = CAMERA_FIELD"framerate";
const string kBitrate = CAMERA_FIELD"bitrate";
const string kDevice = CAMERA_FIELD"device";
onceToken token1([](){
    mINI::Instance()[kDevice] = 0;
    mINI::Instance()[kWidth] = 800;
    mINI::Instance()[kHeight] = 600;
    mINI::Instance()[kFramerate] = 30;
    mINI::Instance()[kBitrate] = 500000;
}, nullptr);
} // namespace Camera 
namespace Microphone {
#define MIC_FIELD "mic."
const string kSamplerate = MIC_FIELD"samplerate";
const string kChannel = MIC_FIELD"channels";
const string kBitrate = MIC_FIELD"bitrate";
const string kDevice = MIC_FIELD"device";
onceToken token1([](){
    mINI::Instance()[kDevice] = 0;
    mINI::Instance()[kSamplerate] = 44100;
    mINI::Instance()[kChannel] = 1;
    mINI::Instance()[kBitrate] = 64000;
}, nullptr);
} // namespace Microphone 

namespace Http {
#define HTTP_FIELD "http."
#define HTTP_PORT 80
const string kPort = HTTP_FIELD"port";
#define HTTPS_PORT 443
const string kSSLPort = HTTP_FIELD"sslport";
onceToken token1([](){
    mINI::Instance()[kPort] = HTTP_PORT;
    mINI::Instance()[kSSLPort] = HTTPS_PORT;
    mINI::Instance()[THEAD_COUNT] = std::thread::hardware_concurrency();
},nullptr);
}//namespace Http

// //////////RTSP服务器配置///////////
namespace Rtsp {
#define RTSP_FIELD "rtsp."
#define RTSP_PORT 554
#define RTSPS_PORT 322
const string kPort = RTSP_FIELD"port";
const string kSSLPort = RTSP_FIELD"sslport";
onceToken token1([](){
    mINI::Instance()[kPort] = RTSP_PORT;
    mINI::Instance()[kSSLPort] = RTSPS_PORT;
},nullptr);

} //namespace Rtsp

// //////////RTMP服务器配置///////////
namespace Rtmp {
#define RTMP_FIELD "rtmp."
#define RTMP_PORT 1935
const string kPort = RTMP_FIELD"port";
onceToken token1([](){
    mINI::Instance()[kPort] = RTMP_PORT;
},nullptr);
} //namespace RTMP
}  // namespace mediakit


int main(int argc,char *argv[]) {

    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().add(std::make_shared<FileChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    SDLCapture cap;
    std::vector<Device> devs, adevs;
    cap.getAudioCaputreDevice(adevs);
    cap.getVideoCaptureDevice(devs);
    if (devs.empty() && adevs.empty()) {
        ErrorL << "no device found!";
        return -1;
    }
    InfoL << adevs.size() << " microphone devices:";
    for (auto& dev: adevs) {
        InfoL << dev.id << "> " << dev.name;
    }
    InfoL << devs.size() << " camera devices:";
    for (auto& dev: devs) {
        InfoL << dev.id << "> " << dev.name;
    }

    loadIniConfig();

    size_t threads = mINI::Instance()[THEAD_COUNT];
    EventPollerPool::setPoolSize(threads);
    WorkThreadPool::setPoolSize(threads);
    // EventPollerPool::enableCpuAffinity(affinity);

    // 加载证书，证书包含公钥和私钥
    SSL_Initor::Instance().loadCertificate((exeDir() + "ssl.p12").data());
    // 信任某个自签名证书
    SSL_Initor::Instance().trustCertificate((exeDir() + "ssl.p12").data());
    // 不忽略无效证书证书(例如自签名或过期证书)
    SSL_Initor::Instance().ignoreInvalidCertificate(false);

    std::string listen_ip = mINI::Instance()[General::kListenIP];
    uint16_t rtspPort = mINI::Instance()[Rtsp::kPort];
    uint16_t rtspsPort = mINI::Instance()[Rtsp::kSSLPort];
    uint16_t rtmpPort = mINI::Instance()[Rtmp::kPort];
    uint16_t httpPort = mINI::Instance()[Http::kPort];
    uint16_t httpsPort = mINI::Instance()[Http::kSSLPort];

    TcpServer::Ptr rtspSrv(new TcpServer());
    TcpServer::Ptr rtmpSrv(new TcpServer());
    TcpServer::Ptr httpSrv(new TcpServer());
    TcpServer::Ptr httpsSrv(new TcpServer());

    rtspSrv->start<RtspSession>(rtspPort, listen_ip);//默认554
    rtmpSrv->start<RtmpSession>(rtmpPort, listen_ip);//默认1935
    httpSrv->start<HttpSession>(httpPort, listen_ip);//默认80
    httpsSrv->start<HttpsSession>(httpsPort, listen_ip);//默认443

    // 支持ssl加密的rtsp服务器，可用于诸如亚马逊echo show这样的设备访问
    TcpServer::Ptr rtspSSLSrv(new TcpServer());
    rtspSSLSrv->start<RtspSessionWithSSL>(rtspsPort, listen_ip);//默认322

    MediaTuple tuple(DEFAULT_VHOST, "live", "camera");
    auto dev = std::make_shared<DevChannel>(tuple);
    uint64_t startTick = SDL_GetTicks();

    GET_CONFIG(uint32_t, device, Microphone::kDevice);
    GET_CONFIG(int, samplerate, Microphone::kSamplerate);
    GET_CONFIG(int, channel, Microphone::kChannel);
    
    if (cap.startAudioRecord(samplerate,channel)) {
        AudioInfo ainfo;
        ainfo.iSampleBit = 16;
        auto& aspec = cap.getRecordSpec();
        ainfo.iSampleRate = aspec.freq;
        ainfo.iChannel = aspec.channels;
        ainfo.codecId = CodecAAC;
        dev->initAudio(ainfo);
        cap.setPcmCallback([dev, startTick](short* pcm, int samples, int channel) {
            dev->inputPCM((char *)pcm, samples * 2 * channel, SDL_GetTicks() - startTick);
        });
    }

    GET_CONFIG(uint32_t, vbitrate, Camera::kBitrate);
    GET_CONFIG(uint32_t, width, Camera::kWidth);
    GET_CONFIG(uint32_t, height, Camera::kHeight);
    GET_CONFIG(uint32_t, framerate, Camera::kFramerate);
    if (devs.size() && cap.startCamera(width, height, framerate, devs[0].id)) {
        VideoInfo vinfo;
        auto& vspec = cap.getVideoSpec();
        vinfo.iWidth = vspec.width;
        vinfo.iHeight = vspec.height;
        vinfo.iFrameRate = vspec.framerate_numerator / vspec.framerate_denominator;
        vinfo.codecId = CodecH264;
        vinfo.iBitRate = vbitrate;
        dev->initVideo(vinfo);

        cap.setYuvCallback([dev, startTick](uint8_t* yuv, int width, int height) {
            char *plan[3];
            int linesize[3];
            int size = width * height;
            plan[0] = (char *)yuv;
            plan[1] = plan[0] + size;
            plan[2] = plan[1] + size / 4;
            linesize[0] = width;
            linesize[1] = width / 2;
            linesize[2] = width / 2;
            dev->inputYUV(plan, linesize, SDL_GetTicks() - startTick);
        });
    }

    getchar();
    return 0;
}

