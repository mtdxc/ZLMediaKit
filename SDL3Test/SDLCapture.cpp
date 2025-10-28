
#include "SDLCapture.h"
#include "Util/logger.h"

std::string ToString(const SDL_AudioSpec &sp) {
    return toolkit::_StrPrinter() << SDL_GetAudioFormatName(sp.format) << " " << sp.freq << "x" << sp.channels;
}

std::string ToString(const SDL_CameraSpec &sp) {
    return toolkit::_StrPrinter() << SDL_GetPixelFormatName(sp.format) << " " << sp.width << "x" << sp.height << "@" << sp.framerate_numerator / sp.framerate_denominator;
}

SDLCapture::SDLCapture() {
    static bool inited = false;
    if (!inited) {
        inited = SDL_Init(SDL_INIT_CAMERA | SDL_INIT_AUDIO | SDL_INIT_VIDEO);
        SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
        SDL_SetLogOutputFunction([](void *userdata, int category, SDL_LogPriority priority, const char *message) {
            DebugL << category << " " <<  priority << message;
        }, nullptr);
        InfoL << "SDL_Init retuned " << inited;
    }
}

int SDLCapture::getAudioCaputreDevice(std::vector<Device> &devList) {
    devList.clear();
    int count = 0;
    auto devs = SDL_GetAudioRecordingDevices(&count);
    if (devs) {
        for (int i = 0; i < count; ++i) {
            Device dev;
            dev.id = devs[i];
            dev.name = SDL_GetAudioDeviceName(devs[i]);
            devList.emplace_back(std::move(dev));
        }
        SDL_free(devs);
    }
    return count;
}

int SDLCapture::getAudioPlayDevice(std::vector<Device> &devList) {
    devList.clear();
    int count = 0;
    auto devs = SDL_GetAudioPlaybackDevices(&count);
    if (devs) {
        for (int i = 0; i < count; ++i) {
            Device dev;
            dev.id = devs[i];
            dev.name = SDL_GetAudioDeviceName(devs[i]);
            devList.emplace_back(std::move(dev));
        }
        SDL_free(devs);
    }
    return count;
}

int SDLCapture::getVideoCaptureDevice(std::vector<Device> &devList) {
    devList.clear();
    int count = 0;
    auto devs = SDL_GetCameras(&count);
    if (devs) {
        for (int i = 0; i < count; ++i) {
            Device dev;
            dev.id = devs[i];
            dev.name = SDL_GetCameraName(devs[i]);
            devList.emplace_back(std::move(dev));
        }
        SDL_free(devs);
    }
    return count;
}

void SDLCapture::stopCamera() {
    if (_camera) {
        SDL_CloseCamera(_camera);
        _camera = nullptr;
    }
    if (_timerID) {
        SDL_RemoveTimer(_timerID);
        _timerID = 0;
    } 
}

bool SDLCapture::startCamera(int width, int height, int fps, uint32_t cid) {
    _cameraSpec.width = width;
    _cameraSpec.height = height;
    _cameraSpec.format = SDL_PIXELFORMAT_IYUV;
    _cameraSpec.colorspace = SDL_COLORSPACE_UNKNOWN;
    _cameraSpec.framerate_numerator = fps;
    _cameraSpec.framerate_denominator = 1;
    stopCamera();
    InfoL << "req camera: " << cid << " " << SDL_GetCameraName(cid) << " with format " << ToString(_cameraSpec);

    _camera = SDL_OpenCamera(cid, &_cameraSpec);
    if (!_camera) {
        return false;
    }

    SDL_GetCameraFormat(_camera, &_cameraSpec);
    fps = _cameraSpec.framerate_numerator / _cameraSpec.framerate_denominator;
    InfoL << "open camera " << cid << " with format " << ToString(_cameraSpec) << ", fps " << fps;
    _timerID = SDL_AddTimer(1000 / fps, [](void *userdata, SDL_TimerID timerID, Uint32 interval) {
        auto loop = static_cast<SDLCapture *>(userdata);
        if (loop) {
            loop->captureFrame();
        }
        return interval;
    }, this);
    return true;
}

bool SDLCapture::startAudioPlay(int sampelrate, int channels, uint32_t id) {
    int frames;
    if (!SDL_GetAudioDeviceFormat(id, &_playSpec, &frames)) {
        ErrorL << id << " GetAudioDeviceFormat failed:" << SDL_GetError();
        return false;
    }
    _playSpec.format = SDL_AUDIO_S16;
    if (sampelrate > 0) 
        _playSpec.freq = sampelrate;
    if (channels > 0) 
        _playSpec.channels = channels;
    stopAudioPlay();
    InfoL << "req speaker " << id << " with param " << ToString(_playSpec) << " " << frames;
    _spkStream = SDL_OpenAudioDeviceStream(id, &_playSpec,
        [](void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
        SDLCapture* loop = (SDLCapture*)userdata;
        if (loop && additional_amount > 0) {
            if (loop->_spkBuffer.size() < (size_t)additional_amount) {
                loop->_spkBuffer.resize(additional_amount);
            }
            uint8_t* buffer = loop->_spkBuffer.data();
            // 读取可用的音频数据
            if (loop->_pcmFillCb) {
                auto channels = loop->_playSpec.channels;
                loop->_pcmFillCb((short *)buffer, additional_amount / 2 / channels, channels);
            }
            else{
                memset(buffer, 0, additional_amount);
            }
            SDL_PutAudioStreamData(stream, buffer, additional_amount);
        }
    }, this);

    if (_spkStream == nullptr) {
        ErrorL << "OpenAudioDeviceStream failed:" << SDL_GetError();
        return false;
    }
    else {
        SDL_AudioSpec inspec;
        SDL_GetAudioStreamFormat(_spkStream, &inspec, &_playSpec);
        InfoL << id << " stream param: " << ToString(inspec) << " -> " << ToString(_playSpec);
        SDL_ResumeAudioStreamDevice(_spkStream);
        return true;
    }
}

void SDLCapture::stopAudioPlay() {
    if (_spkStream) {
        SDL_DestroyAudioStream(_spkStream);
        _spkStream = nullptr;
    }
}

void SDLCapture::stopAudioRecord() {
    if (_micStream) {
        SDL_DestroyAudioStream(_micStream);
        _micStream = nullptr;
    }
}

bool SDLCapture::startAudioRecord(int sampelrate, int channels, uint32_t id) {
    int frames;
    if (!SDL_GetAudioDeviceFormat(id, &_recordSpec, &frames)) {
        ErrorL << id << " GetAudioDeviceFormat failed:" << SDL_GetError();
        return false;
    }
    _recordSpec.format = SDL_AUDIO_S16;
    if (sampelrate > 0) 
        _recordSpec.freq = sampelrate;
    if (channels > 0) 
        _recordSpec.channels = channels;
    stopAudioRecord();
    InfoL << "req microphone " << id << " with param " << ToString(_recordSpec) << " " << frames;
    _micStream = SDL_OpenAudioDeviceStream(id, &_recordSpec,
        [](void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
        SDLCapture* loop = (SDLCapture*)userdata;
        // 当有新的音频数据可用时调用
        if (loop && additional_amount > 0) {
            // 读取可用的音频数据
            if (loop->_micBuffer.size() < (size_t)additional_amount) {
                loop->_micBuffer.resize(additional_amount);
            }
            uint8_t* buffer = loop->_micBuffer.data();
            int got = SDL_GetAudioStreamData(stream, buffer, additional_amount);
            if (got > 0 && loop->_pcmCallback) {
                auto channels = loop->_recordSpec.channels;
                loop->_pcmCallback((short *)buffer, got / 2 / channels, channels);
            }
        }
    }, this);

    if (_micStream == nullptr) {
        ErrorL << "OpenAudioDeviceStream failed:" << SDL_GetError();
        return false;
    }
    else {
        SDL_AudioSpec inspec;
        SDL_GetAudioStreamFormat(_micStream, &inspec, &_recordSpec);
        InfoL << id << " stream param: " << ToString(inspec) << " -> " << ToString(_recordSpec);
        SDL_ResumeAudioStreamDevice(_micStream);
        return true;
    }
}

void SDLCapture::captureFrame() {
    if (!_camera || !_yuvCallback) return; 
    Uint64 tsp;
    SDL_Surface* frame = SDL_AcquireCameraFrame(_camera, &tsp);
    if (frame) {
        if (frame->format != SDL_PIXELFORMAT_IYUV) {
            WarnL << "unkonwn format " << SDL_GetPixelFormatName(frame->format);
        } else {
            _yuvCallback((uint8_t*)frame->pixels, frame->w, frame->h);
#if YUV_DUMP
            static int count = 0;
            if (count++ < 10) {
                char path[64];
                sprintf(path, "%dx%d_%d.yuv", frame->w, frame->h, count);
                if (FILE *fp = fopen(path, "wb")) {
                    fwrite(frame->pixels, 1, size * 3 / 2, fp);
                    fclose(fp);
                }
            }
#endif
        }
        SDL_ReleaseCameraFrame(_camera, frame);
    }
}
