#ifndef ZLMEDIAKIT_WEMB_H
#define ZLMEDIAKIT_WEMB_H

#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include "Extension/Frame.h"
#include "Extension/Track.h"
#include "Common/MediaSink.h"
#include "Util/ResourcePool.h"

struct mkv_buffer_t;
struct mkv_reader_t;
struct mkv_writer_t;
namespace mediakit {

// This class is used to generate WebM files
class WebmMuxer : public MuxerInterface {
public:
    WebmMuxer();
    ~WebmMuxer() override;

    bool open(const std::string &file) override;
    void close() override;
    bool opened() const { return _context != nullptr; }
    /**
     * 添加音视频轨道
     * Add audio and video tracks
     */
    bool addTrack(const Track::Ptr &track) override;

    /**
     * 重置音视频轨道
     * Reset audio and video tracks     
     */
    void resetTracks() override;

    /**
     * 输入帧数据
     * Input frame data     
     */
    bool inputFrame(const Frame::Ptr &frame) override;

    /**
     * 刷新输出所有frame缓存
     * Flush all frame buffers in the output
     */
    void flush() override {}

    uint64_t getDuration() const override;
private:
    bool _started = false;
    bool _have_video = false;
    mkv_writer_t *_context = nullptr;
    FILE *_file = nullptr;
    std::string _file_path;

    class FrameMergerImp : public FrameMerger {
    public:
        FrameMergerImp() : FrameMerger(FrameMerger::mp4_nal_size) {}
    };

    struct MkvTrack {
        int track_id = -1;
        Stamp stamp;
        FrameMergerImp merger;
    };
    std::unordered_map<int, MkvTrack> _tracks;
    toolkit::ResourcePool<toolkit::BufferRaw> _buffer_pool;
};

class WebmDemuxer : public TrackSource {
public:
    using Ptr = std::shared_ptr<WebmDemuxer>;

    ~WebmDemuxer() override;

    /**
     * 打开文件
     * @param file 文件路径
     */
    bool open(const std::string &file);
    void close();
    bool opened() const { return _context != nullptr; }

    /**
     * 移动时间轴至某处
     * @param stamp_ms 预期的时间轴位置，单位毫秒
     * @return 时间轴位置
     */
    int64_t seekTo(int64_t stamp_ms);

    /**
     * 读取一帧数据
     * @param keyFrame 是否为关键帧
     * @param eof 是否文件读取完毕
     * @return 帧数据,可能为空
     */
    Frame::Ptr readFrame(bool &keyFrame, bool &eof);

    /**
     * 获取所有Track信息
     * @param trackReady 是否要求track为就绪状态
     * @return 所有Track
     */
    std::vector<Track::Ptr> getTracks(bool trackReady) const override;

    /**
     * 获取文件时长
     * @return 文件时长，单位毫秒
     */
    uint64_t getDuration() const { return _duration_ms;}

private:
    int getAllTracks();
    void onVideoTrack(uint32_t track_id, int object, int width, int height, const void *extra, size_t bytes);
    void onAudioTrack(uint32_t track_id, int object, int channel_count, int bit_per_sample, int sample_rate, const void *extra, size_t bytes);
    Frame::Ptr makeFrame(uint32_t track_id, const toolkit::Buffer::Ptr &buf, int64_t pts, int64_t dts);

private:
    uint64_t _duration_ms = 0;
    mkv_reader_t* _context = nullptr;
    FILE* _file = nullptr;
    std::unordered_map<int, Track::Ptr> _tracks;
    toolkit::ResourcePool<toolkit::BufferRaw> _buffer_pool;
};

}//mediakit

#endif //ZLMEDIAKIT_WEMB_H