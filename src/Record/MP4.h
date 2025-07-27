/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_MP4_H
#define ZLMEDIAKIT_MP4_H

#if defined(ENABLE_MP4)

#include <memory>
#include <string>
#include "mpeg4-hevc.h"
#include "mpeg4-avc.h"
#include "mpeg4-aac.h"
#include "mov-buffer.h"
#include "mov-format.h"
#include "mkv-buffer.h"
#include "mkv-reader.h"
#include "mov-reader.h"
#include "fmp4-writer.h"
#include "mov-writer.h"
#include "mkv-writer.h"
#include "Extension/Frame.h"
namespace mediakit {

struct mp4_writer_t {
    mov_writer_t *mov;
    fmp4_writer_t* fmp4;
    mkv_writer_t* mkv;
};

/// @param[in] flags mov flags, such as: MOV_FLAG_SEGMENT, see more @mov-format.h
struct mp4_writer_t* mp4_writer_create(int type, const struct mov_buffer_t* buffer, void* param, int flags);
void mp4_writer_destroy(struct mp4_writer_t* mp4);

/// @param[in] object MPEG-4 systems ObjectTypeIndication such as: MOV_OBJECT_AAC, see more @mov-format.h
/// @param[in] extra_data AudioSpecificConfig
/// @return >=0-track, <0-error
int mp4_writer_add_audio(struct mp4_writer_t* mp4, CodecId object, int channel_count, int bits_per_sample, int sample_rate, const void* extra_data, size_t extra_data_size);

/// @param[in] object MPEG-4 systems ObjectTypeIndication such as: MOV_OBJECT_H264, see more @mov-format.h
/// @param[in] extra_data AVCDecoderConfigurationRecord/HEVCDecoderConfigurationRecord
/// @return >=0-track, <0-error
int mp4_writer_add_video(struct mp4_writer_t* mp4, CodecId object, int width, int height, const void* extra_data, size_t extra_data_size);

int mp4_writer_add_subtitle(struct mp4_writer_t* mp4, CodecId object, const void* extra_data, size_t extra_data_size);

/// Write audio/video stream
/// raw AAC data, don't include ADTS/AudioSpecificConfig
/// H.264/H.265 MP4 format, replace start code(0x00000001) with NALU size
/// @param[in] track return by mov_writer_add_audio/mov_writer_add_video
/// @param[in] data audio/video frame
/// @param[in] bytes buffer size
/// @param[in] pts timestamp in millisecond
/// @param[in] dts timestamp in millisecond
/// @param[in] flags MOV_AV_FLAG_XXX, such as: MOV_AV_FLAG_KEYFREAME, see more @mov-format.h
/// @return 0-ok, other-error
int mp4_writer_write(struct mp4_writer_t* mp4, int track, const void* data, size_t bytes, int64_t pts, int64_t dts, int flags);

///////////////////// The following interfaces are only applicable to fmp4 ///////////////////////////////

/// Save data and open next segment
/// @return 0-ok, other-error
int mp4_writer_save_segment(struct mp4_writer_t* mp4);

/// Get init segment data(write FTYP, MOOV only)
/// WARNING: it caller duty to switch file/buffer context with fmp4_writer_write
/// @return 0-ok, other-error
int mp4_writer_init_segment(struct mp4_writer_t* mp4);


// mp4文件IO的抽象接口类  [AUTO-TRANSLATED:dab24105]
// Abstract interface class for mp4 file IO
class MP4FileIO : public std::enable_shared_from_this<MP4FileIO> {
public:
    using Ptr = std::shared_ptr<MP4FileIO>;
    using Writer = std::shared_ptr<mp4_writer_t>;
    using Reader = std::shared_ptr<mov_reader_t>;
    using WebmWriter = std::shared_ptr<mkv_writer_t>;
    using WebmReader = std::shared_ptr<mkv_reader_t>;

    virtual ~MP4FileIO() = default;

    /**
     * 创建mp4复用器
     * @param flags 支持0、MOV_FLAG_FASTSTART、MOV_FLAG_SEGMENT
     * @param type 类型 
     * - 2 webm
     * - 1 fmp4
     * - 0 普通mp4
     * @return mp4复用器
     * Create an mp4 muxer
     * @param flags Supports 0, MOV_FLAG_FASTSTART, MOV_FLAG_SEGMENT, MKV_OPTION_WEBM
     * @param type Whether it is fmp4 or ordinary mp4
     * @return mp4 muxer
     
     * [AUTO-TRANSLATED:97fefe95]
     */
    virtual Writer createWriter(int flags, int type);

    /**
     * 创建mp4解复用器
     * @return mp4解复用器
     * Create an mp4 demuxer
     * @return mp4 demuxer
     
     * [AUTO-TRANSLATED:4a303019]
     */
    virtual Reader createReader();
    virtual WebmReader createWebmReader();

    /**
     * 获取文件读写位置
     * Get the file read/write position
     
     * [AUTO-TRANSLATED:f8a5b290]
     */
    virtual int64_t onTell() = 0;

    /**
     * seek至文件某处
     * @param offset 文件偏移量
     * @return 是否成功(0成功)
     * Seek to a certain location in the file
     * @param offset File offset
     * @return Whether it is successful (0 successful)
     
     * [AUTO-TRANSLATED:936089eb]
     */
    virtual int onSeek(int64_t offset) = 0;

    /**
     * 从文件读取一定数据
     * @param data 数据存放指针
     * @param bytes 指针长度
     * @return 是否成功(0成功)
     * Read a certain amount of data from the file
     * @param data Data storage pointer
     * @param bytes Pointer length
     * @return Whether it is successful (0 successful)
     
     * [AUTO-TRANSLATED:926bf3f0]
     */
    virtual int onRead(void *data, size_t bytes) = 0;

    /**
     * 写入文件一定数据
     * @param data 数据指针
     * @param bytes 数据长度
     * @return 是否成功(0成功)
     * Write a certain amount of data to the file
     * @param data Data pointer
     * @param bytes Data length
     * @return Whether it is successful (0 successful)
     
     * [AUTO-TRANSLATED:dc0abb95]
     */
    virtual int onWrite(const void *data, size_t bytes) = 0;
};

// 磁盘MP4文件类  [AUTO-TRANSLATED:e3f5ac07]
// Disk MP4 file class
class MP4FileDisk : public MP4FileIO {
public:
    using Ptr = std::shared_ptr<MP4FileDisk>;

    /**
     * 打开磁盘文件
     * @param file 文件路径
     * @param mode fopen的方式
     * Open the disk file
     * @param file File path
     * @param mode fopen mode
     
     * [AUTO-TRANSLATED:c3144f10]
     */
    void openFile(const char *file, const char *mode);

    /**
     * 关闭磁盘文件
     * Close the disk file
     
     * [AUTO-TRANSLATED:fc6b4f50]
     */
    void closeFile();

protected:
    int64_t onTell() override;
    int onSeek(int64_t offset) override;
    int onRead(void *data, size_t bytes) override;
    int onWrite(const void *data, size_t bytes) override;

private:
    std::shared_ptr<FILE> _file;
};

class MP4FileMemory : public MP4FileIO{
public:
    using Ptr = std::shared_ptr<MP4FileMemory>;

    /**
     * 获取文件大小
     * Get the file size
     
     * [AUTO-TRANSLATED:3a2b682a]
     */
    size_t fileSize() const;

    /**
     * 获取并清空文件缓存
     * Get and clear the file cache
     
     
     * [AUTO-TRANSLATED:620d5cf6]
     */
    std::string getAndClearMemory();

protected:
    int64_t onTell() override;
    int onSeek(int64_t offset) override;
    int onRead(void *data, size_t bytes) override;
    int onWrite(const void *data, size_t bytes) override;

private:
    int64_t _offset = 0;
    std::string _memory;
};

}//namespace mediakit
#endif //defined(ENABLE_MP4)
#endif //ZLMEDIAKIT_MP4_H
