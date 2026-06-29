
#include "SDLCapture.h"
#include "Util/logger.h"

std::string ToString(const SDL_AudioSpec &sp) {
    char line[64];
    snprintf(line, sizeof(line), "%s %dx%d", SDL_GetAudioFormatName(sp.format), sp.freq, sp.channels);
    return line;
}

std::string ToString(const SDL_CameraSpec &sp) {
    char line[64];
    snprintf(line, sizeof(line), "%s %dx%d@%d", SDL_GetPixelFormatName(sp.format), sp.width, sp.height, sp.framerate_numerator / sp.framerate_denominator);
    return line;
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
    setVideoCapturePaused(false);
    return true;
}

void SDLCapture::setVideoCapturePaused(bool paused) {
    if (!_camera) return ;
    if (paused) {
        if (_timerID) {
            SDL_RemoveTimer(_timerID);
            _timerID = 0;
        }
    } else if(!_timerID) {
        int fps = _cameraSpec.framerate_numerator / _cameraSpec.framerate_denominator;
        _timerID = SDL_AddTimer(1000 / fps, [](void *userdata, SDL_TimerID timerID, Uint32 interval) {
            auto loop = static_cast<SDLCapture *>(userdata);
            if (loop) {
                loop->captureFrame();
            }
            return interval;
        }, this);
    }
}

void SDLCapture::setPaused(SDL_AudioStream* stream, bool paused) {
    if (!stream) return;
    if (paused)
        SDL_PauseAudioStreamDevice(stream);
    else if(SDL_AudioStreamDevicePaused(stream))
        SDL_ResumeAudioStreamDevice(stream);
}

bool SDLCapture::isPaused(SDL_AudioStream* stream) {
    if(!stream) return true;
    return SDL_AudioStreamDevicePaused(stream);
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
            auto channels = loop->_playSpec.channels;
            // 读取可用的音频数据
            if (!loop->onPcmFill((short *)buffer, additional_amount / 2 / channels, channels)) {
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
        SDL_AudioSpec outspec;
        SDL_GetAudioStreamFormat(_spkStream, &_playSpec, &outspec);
        InfoL << id << " stream param: " << ToString(_playSpec) << " -> " << ToString(outspec);
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
            if (got > 0) {
                auto channels = loop->_recordSpec.channels;
                loop->onPcm((short *)buffer, got / 2 / channels, channels);
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
    if (!_camera) return; 
    Uint64 tsp;
    SDL_Surface* frame = SDL_AcquireCameraFrame(_camera, &tsp);
    if (frame) {
        if (frame->format != SDL_PIXELFORMAT_IYUV) {
            WarnL << "unkonwn format " << SDL_GetPixelFormatName(frame->format);
        } else {
            if (_preview) {
                _preview->display(frame);
            }
            onYuv((uint8_t*)frame->pixels, frame->w, frame->h);
        }
        SDL_ReleaseCameraFrame(_camera, frame);
    }
}

///////////////////////////////////
// YuvDisplayer
YuvDisplayer::~YuvDisplayer() {
    if (_texture) {
        SDL_DestroyTexture(_texture);
        _texture = nullptr;
    }
    if (_render) {
        SDL_DestroyRenderer(_render);
        _render = nullptr;
    }
    if (_win) {
        SDL_DestroyWindow(_win);
        _win = nullptr;
    }
}

void YuvDisplayer::checkWind(int width, int height) {
    if (!_win) {
        if (_hwnd) {
            SDL_PropertiesID props = SDL_CreateProperties();
            // 根据平台设置正确的属性
            const char *platform = SDL_GetPlatform();

            if (strcmp(platform, "Windows") == 0) {
                SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, _hwnd);
            }
            else if (strcmp(platform, "Linux") == 0) {
                // 尝试 X11
                // SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_X11_WINDOW_POINTER, _hwnd);
            }
            else if (strcmp(platform, "macOS") == 0) {
                SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_COCOA_WINDOW_POINTER, _hwnd);
            }
            _win = SDL_CreateWindowWithProperties(props);
            SDL_DestroyProperties(props);
        }
        else {
            _win = SDL_CreateWindow(_title.data(), width, height, SDL_WINDOW_OPENGL);
        }
    }
    if (_win && !_render) {
        _render = SDL_CreateRenderer(_win, "direct3d,opengl,software"); // "direct3d", "metal", "software", "opengl"
    }
    if (_render && (!_texture || _texture->w != width || _texture->h != height)) {
        if (_texture) {
            SDL_DestroyTexture(_texture);
            _texture = nullptr;
        }
        _texture = SDL_CreateTexture(_render, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
    }
}

bool YuvDisplayer::display(SDL_Surface *frame) {
    checkWind(frame->w, frame->h);
    if (_texture && frame->format == SDL_PIXELFORMAT_IYUV) {
        int stride = frame->pitch ? frame->pitch : frame->w;
        int size = stride * frame->h;
        uint8_t *pixels = (uint8_t *)frame->pixels;
        SDL_UpdateYUVTexture(_texture, nullptr, pixels, stride, pixels + size, stride / 2, pixels + size * 5 / 4, stride / 2);
        SDL_RenderClear(_render);
        SDL_RenderTexture(_render, _texture, nullptr, nullptr);
        SDL_RenderPresent(_render);
        return true;
    }
    return false;
}

#ifdef ENABLE_FFMPEG
#include "libavutil/frame.h"
#endif

bool YuvDisplayer::displayYUV(AVFrame *pFrame) {
#ifdef ENABLE_FFMPEG
    checkWind(pFrame->width, pFrame->height);
    if (_texture) {
        SDL_UpdateYUVTexture(_texture, nullptr, 
            pFrame->data[0], pFrame->linesize[0], 
            pFrame->data[1], pFrame->linesize[1], 
            pFrame->data[2], pFrame->linesize[2]);

        // SDL_UpdateTexture(_texture, nullptr, pFrame->data[0], pFrame->linesize[0]);
        SDL_RenderClear(_render);
        SDL_RenderTexture(_render, _texture, nullptr, nullptr);
        SDL_RenderPresent(_render);
        return true;
    }
    return false;
#endif // ENABLE_FFMPEG
}
