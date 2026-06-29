/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <signal.h>
#include "Util/logger.h"
#include "Common/config.h"
#include "Player/MediaPlayer.h"
#include "Codec/Transcode.h"
#include "SDL3/SDL_Main.h"
#include "libavcodec/avcodec.h"
#include "SDLCapture.h"

using namespace std;
using namespace toolkit;
using namespace mediakit;


SDLCapture loop;
int main(int argc, char *argv[]) {
#ifdef _WIN32
    // 1. 首先调用AllocConsole创建一个控制台窗口
    AllocConsole();

    // 2. 但此时调用cout或者printf都不能正常输出文字到窗口（包括输入流cin和scanf）, 所以需要如下重定向输入输出流：
    FILE *stream;
    freopen_s(&stream, "CON", "r", stdin); //重定向输入流
    freopen_s(&stream, "CON", "w", stdout); //重定向输入流

    // 清除流缓冲区, 在win11上还是无法输出文字，需要在加入如下代码
    std::cin.clear();
    std::cout.clear();

    // 3. 如果我们需要用到控制台窗口句柄，可以调用FindWindow取得：
    SetConsoleTitleA(argv[0]); //设置窗口名
#endif 

    // 设置退出信号处理函数
    signal(SIGINT, [](int) { loop.shutdown(); });
    // 设置日志
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    if (argc < 2) {
        std::cout << "usage:" << argv[0] << "rtxp_url[rtp_type][play_track]\r\n example:"
                << argv[0] << " rtsp://admin:123456@127.0.0.1/live/0 0\r\n";
        return 0;
    }

    char *url = argv[1];
    auto player = std::make_shared<MediaPlayer>();
    // sdl要求在main线程初始化
    auto displayer = std::make_shared<YuvDisplayer>(nullptr, url);
    weak_ptr<MediaPlayer> weakPlayer = player;
    player->setOnPlayResult([weakPlayer, displayer](const SockException &ex) {
        InfoL << "OnPlayResult:" << ex.what();
        auto strongPlayer = weakPlayer.lock();
        if (ex || !strongPlayer) {
            return;
        }

        auto videoTrack = dynamic_pointer_cast<VideoTrack>(strongPlayer->getTrack(TrackVideo, false));
        auto audioTrack = dynamic_pointer_cast<AudioTrack>(strongPlayer->getTrack(TrackAudio, false));

        if (videoTrack) {
            videoTrack->addRawDelegate([displayer](const FFmpegFrame::Ptr &yuv) {
                loop.doTask([yuv, displayer]() {
                    // sdl要求在main线程渲染
                    displayer->displayYUV(yuv.get());
                    return true;
                });
            });
        }

        if (audioTrack) {
            //FFmpeg解码时已经统一转换为16位整型pcm
            auto pcms = std::make_shared<PcmBuffer<short>>();
            loop.startAudioPlay(audioTrack->getAudioSampleRate(), audioTrack->getAudioChannel());
            loop.setPcmFillCallback([pcms](short* pcm, int samples, int channel) { 
                pcms->Read(pcm, samples * channel);
            });
            FFmpegSwr::Ptr swr;
            audioTrack->addRawDelegate(
                [swr, pcms](const FFmpegFrame::Ptr &frame) mutable {
                if (!swr) {
#if LIBAVCODEC_VERSION_INT >= FF_CODEC_VER_7_1
                    swr = std::make_shared<FFmpegSwr>(AV_SAMPLE_FMT_S16, &(frame->ch_layout), frame->sample_rate);
#else
                    swr = std::make_shared<FFmpegSwr>(AV_SAMPLE_FMT_S16, frame->channels, frame->channel_layout, frame->sample_rate);
#endif
                }
                auto pcm = swr->inputFrame(frame);
                auto len = pcm->nb_samples * FFmpegFrame::getChannels(pcm);// * av_get_bytes_per_sample((enum AVSampleFormat)pcm->format);
                pcms->Write((short*)pcm->data[0], len);
                //audio_player->playPCM((const char *)(pcm->data[0]), MIN(len, frame->linesize[0]));
            });
        }
    });

    player->setOnShutdown([](const SockException &ex) { WarnL << "play shutdown: " << ex.what(); });

    // 不等待track ready再回调播放成功事件，这样可以加快秒开速度
    (*player)[Client::kWaitTrackReady] = false;
    if (argc > 2) {
        (*player)[Client::kRtpType] = atoi(argv[2]);
    }
    if (argc > 3) {
        (*player)[Client::kPlayTrack] = atoi(argv[3]);
    }
    player->play(url);

    loop.runLoop();

    sleep(1);
    return 0;
}

