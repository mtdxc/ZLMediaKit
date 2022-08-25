/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_MP4MUXER_H
#define ZLMEDIAKIT_MP4MUXER_H

#ifdef ENABLE_MP4

#include "Common/MediaSink.h"
#include "Common/Stamp.h"
//#include "MP4.h"
#include "Ap4.h"
class AP4_Sample;
class AP4_ByteStream;
class AP4_SyntheticSampleTable;
namespace mediakit{
/*
Mp4写入基类.
将MediaSinkInterface写到Mp4文件中
*/
class MP4MuxerInterface : public MediaSinkInterface {
public:
    MP4MuxerInterface() = default;
    ~MP4MuxerInterface() override = default;

    /**
     * 添加已经ready状态的track
     * - mp4_writer_add_audio
     * - mp4_writer_add_video
     */
    bool addTrack(const Track::Ptr &track) override;

    /**
     * 输入帧
     * - 视频合帧
     * - 音频时间戳同步
     * - mp4_writer_write
     */
    bool inputFrame(const Frame::Ptr &frame) override;

    /**
     * 重置所有track
     */
    void resetTracks() override;

    /**
     * 刷新输出所有frame缓存
     */
    void flush() override;

    /**
     * 是否包含视频
     */
    bool haveVideo() const;
    
    uint64_t getDuration() const;
private:
    void stampSync();

protected:
    bool _started = false;
    bool _have_video = false;

    struct track_info {
        AP4_SyntheticSampleTable* sample_table = nullptr;
        Stamp       stamp;
        uint64_t    last_tsp = 0;
        CodecId               codec;
        AP4_UI32              m_TrackId = 0;
        AP4_UI32              m_Timescale = 1000;
        AP4_UI64              m_SampleStartNumber = 0;
        AP4_UI64              m_MediaTimeOrigin = 0;
        AP4_UI64              m_MediaStartTime = 0;
        AP4_UI64              m_MediaDuration = 0;
        AP4_Array<AP4_Sample> m_Samples;
        int WriteMediaSegment(AP4_ByteStream& stream, unsigned int sequence_number);
        AP4_Result AddSample(AP4_Sample sample);
    };
    std::unordered_map<int, track_info> _codec_to_trackid;
    
    AP4_ByteStream* _file_stream = nullptr;
    FrameMerger _frame_merger{FrameMerger::mp4_nal_size};
};

/*
 写Mp4到文件
 正常mp4格式，并可设置faststart
*/
class MP4Muxer : public MP4MuxerInterface{
public:
    typedef std::shared_ptr<MP4Muxer> Ptr;

    MP4Muxer() = default;
    ~MP4Muxer() override;

    /**
     * 重置所有track
     */
    void resetTracks() override;

    /**
     * 打开mp4
     * @param file 文件完整路径
     */
    void openMP4(const std::string &file);

    /**
     * 手动关闭文件(对象析构时会自动关闭)
     */
    void closeMP4();

private:
    std::string _file_name;
};

/// 写fmp4到内存
class MP4MuxerMemory : public MP4MuxerInterface{
public:
    MP4MuxerMemory();
    ~MP4MuxerMemory() override = default;

    /**
     * 重置所有track
     */
    void resetTracks() override;

    /**
     * 输入帧
     * 最终会导致onSegmentData回调
     */
    bool inputFrame(const Frame::Ptr &frame) override;

    /**
     * 获取fmp4 init segment
     */
    const std::string &getInitSegment();

protected:
    /**
     * fmp4切片输出回调函数
     * @param std::string 切片内容
     * @param stamp 切片末尾时间戳
     * @param key_frame 是否有关键帧
     */
    virtual void onSegmentData(std::string string, uint64_t stamp, bool key_frame) = 0;
    
private:
    bool _key_frame = false;
    std::string _init_segment;
    unsigned int _seq_no = 0;
};


}//namespace mediakit
#endif//#ifdef ENABLE_MP4
#endif //ZLMEDIAKIT_MP4MUXER_H
