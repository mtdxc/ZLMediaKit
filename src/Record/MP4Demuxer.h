/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_MP4DEMUXER_H
#define ZLMEDIAKIT_MP4DEMUXER_H

#ifdef ENABLE_MP4
#include "Extension/Track.h"

class AP4_File;
class AP4_Track;
class AP4_DataBuffer;
class AP4_Sample;

namespace mediakit {
/*!
Mp4文件读取类
Mp4File -> TrackSource
*/
class MP4Demuxer : public TrackSource {
public:
    typedef std::shared_ptr<MP4Demuxer> Ptr;

    /**
     * 创建mp4解复用器
     */
    MP4Demuxer();
    ~MP4Demuxer() override;

    /**
     * 打开文件
     * @param file mp4文件路径
     */
    void openMP4(const std::string &file);

    /**
     * @brief 关闭 mp4 文件
     */
    void closeMP4();

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
     * 获取文件长度
     * @return 文件长度，单位毫秒
     */
    uint64_t getDurationMS() const;

private:
    int getAllTracks();
    Frame::Ptr makeFrame(uint32_t track_id, const toolkit::Buffer::Ptr &buf, int64_t pts, int64_t dts);

private:
    AP4_File *_file = nullptr;
    uint64_t _duration_ms = 0;
    struct Context {
        Track::Ptr _track;
        AP4_Track* track;

        bool _eof = false;
        unsigned int index = 0;
        std::shared_ptr<AP4_Sample> sample;
        std::shared_ptr<AP4_DataBuffer> data;
        bool ReadSample();
        bool Seek(int64_t stamp_ms);
    };
    std::vector<Context> _tracks;
};


}//namespace mediakit
#endif//ENABLE_MP4
#endif //ZLMEDIAKIT_MP4DEMUXER_H
