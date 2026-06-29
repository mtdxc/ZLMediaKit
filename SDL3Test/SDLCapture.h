#ifndef TESTS_SDLCAPTURE_H
#define TESTS_SDLCAPTURE_H

#include <stdint.h>
#include <SDL3/SDL.h>
#include <string>
#include <functional>
#include <vector>
#include <deque>
#include <mutex>
#define REFRESH_EVENT   (SDL_EVENT_USER + 1)

template <class T>
class PcmBuffer {
    std::vector<T> buffer_;
    std::mutex lock_;
    int max_size_ = 0;

public:
    PcmBuffer(int max_size = 40960)
        : max_size_(max_size) {}
    void clear() {
        std::unique_lock<decltype(lock_)> l(lock_);
        buffer_.clear();
    }
    T* data() const {
        std::unique_lock<decltype(lock_)> l(lock_);
        return buffer_.data();
    }
    int size() const {
        std::unique_lock<decltype(lock_)> l(lock_);
        return buffer_.size();
    }
    int Read(T *p, int samples, bool zero = true) {
        std::unique_lock<decltype(lock_)> l(lock_);
        const int ElemSize = sizeof(T);
        int new_size = buffer_.size() - samples;
        if (new_size < 0) {
            memcpy(p, buffer_.data(), ElemSize * buffer_.size());
            if (zero) {
                memset(p + buffer_.size(), 0, -ElemSize * new_size);
            }
            buffer_.clear();
        } else {
            memcpy(p, buffer_.data(), ElemSize * samples);
            if (new_size) {
                memmove(buffer_.data(), &buffer_[samples], ElemSize * new_size);
            }
            buffer_.resize(new_size);
        }
        return samples;
    }
    int Write(const T *p, int samples) {
        std::unique_lock<decltype(lock_)> l(lock_);
        if (buffer_.size() > max_size_) {
            buffer_.clear();
        }
        buffer_.insert(buffer_.end(), p, p + samples);
        return samples;
    }
};

struct AVFrame;
class YuvDisplayer {
public:
    using Ptr = std::shared_ptr<YuvDisplayer>;

    YuvDisplayer(void *hwnd = nullptr, const char *title = "untitled") {
        _title = title;
        _hwnd = hwnd;
    }

    virtual ~YuvDisplayer();

    bool display(SDL_Surface* frame);
    bool displayYUV(AVFrame* pFrame);

private:
    void checkWind(int width, int height);
    
    std::string _title;
    void *_hwnd = nullptr;
    SDL_Window *_win = nullptr;
    SDL_Renderer *_render = nullptr;
    SDL_Texture *_texture = nullptr;
};

struct Device {
    uint32_t id;
    std::string name;
};

class SDLCapture {
public:
    static int getAudioCaputreDevice(std::vector<Device> &devList);
    static int getAudioPlayDevice(std::vector<Device> &devList);
    static int getVideoCaptureDevice(std::vector<Device> &devList);

    using PcmCallback = std::function<void(short* pcm, int samples, int channel)>;
    bool startAudioRecord(int sampelrate, int channels, uint32_t id = SDL_AUDIO_DEVICE_DEFAULT_RECORDING);
    void stopAudioRecord();
    virtual void onPcm(short* pcm, int samples, int channel) {
        if (_pcmCallback) _pcmCallback(pcm, samples, channel);
    }
    void setPcmCallback(PcmCallback cb) {
        _pcmCallback = cb;
    }

    static void setPaused(SDL_AudioStream* stream, bool paused);
    static bool isPaused(SDL_AudioStream* stream);
    void setAudioRecordPaused(bool paused) {
        return setPaused(_micStream, paused);
    }
    bool isAudioRecordPaused() {
        return isPaused(_micStream);
    }
    void setAudioPlayPaused(bool paused) {
        return setPaused(_spkStream, paused);
    }
    bool isAudioPlayPaused() {
        return isPaused(_spkStream);
    }
    bool isVideoCapturePaused() {
        return _timerID == 0;
    }
    void setVideoCapturePaused(bool paused);

    bool startAudioPlay(int sampelrate, int channels, uint32_t id = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    void stopAudioPlay();
    virtual bool onPcmFill(short* pcm, int samples, int channel) {
        if (_pcmFillCb) {
            _pcmFillCb(pcm, samples, channel);
            return true;
        }
        else {
            return false;
        }
    }
    void setPcmFillCallback(PcmCallback cb) {
        _pcmFillCb = cb;
    }

    using YuvCallback = std::function<void(uint8_t* yuv, int width, int height)>;
    bool startCamera(int width, int height, int fps, uint32_t id);
    void stopCamera();
    void captureFrame();
    void setPreview(bool preview) {
        if (preview) {
            _preview = std::make_shared<YuvDisplayer>(nullptr, "preview");
        }
        else{
            _preview = nullptr;
        }
    }
    virtual void onYuv(uint8_t* yuv, int width, int height) {
        if (_yuvCallback) _yuvCallback(yuv, width, height);
    }
    void setYuvCallback(YuvCallback cb) {
        _yuvCallback = cb;
    }
    const SDL_CameraSpec& getVideoSpec() const {
        return _cameraSpec;
    }
    const SDL_AudioSpec& getRecordSpec() const {
        return _recordSpec;
    }
    const SDL_AudioSpec& getPlaySpec() const {
        return _playSpec;
    }

    SDLCapture();
    ~SDLCapture() {
        stopAudioRecord();
        stopAudioPlay();
        stopCamera();
        setPreview(false);
        shutdown();
    }

    template<typename FUN>
    void doTask(FUN &&f) {
        {
            std::lock_guard<std::mutex> lck(_mtxTask);
            _taskList.emplace_back(f);
        }
        SDL_Event event;
        event.type = REFRESH_EVENT;
        SDL_PushEvent(&event);
    }

    void runLoop() {
        bool flag = true;
        std::function<bool ()> task;
        SDL_Event event;
        while (flag) {
            SDL_WaitEvent(&event);
            switch (event.type) {
                case REFRESH_EVENT:
                    {
                        std::lock_guard<std::mutex> lck(_mtxTask);
                        if (_taskList.empty()) {
                            // not reachable
                            continue;
                        }
                        task = _taskList.front();
                        _taskList.pop_front();
                    }
                    flag = task();
                    break;
                case SDL_EVENT_QUIT:
                    return;
                default:
                    break;
            }
        }
    }

    void shutdown() {
        doTask([](){return false;});
    }
private:
    std::deque<std::function<bool ()> > _taskList;
    std::mutex _mtxTask;

    YuvDisplayer::Ptr _preview;
    YuvCallback _yuvCallback;
    SDL_Camera* _camera = nullptr;
    SDL_CameraSpec _cameraSpec;
    SDL_TimerID _timerID = 0;

    PcmCallback _pcmCallback;
    SDL_AudioStream* _micStream = nullptr;
    SDL_AudioSpec _recordSpec;
    std::vector<uint8_t> _micBuffer, _spkBuffer; 

    PcmCallback _pcmFillCb;
    SDL_AudioStream* _spkStream = nullptr;
    SDL_AudioSpec _playSpec;
};

#endif //TESTS_SDLCAPTURE_H