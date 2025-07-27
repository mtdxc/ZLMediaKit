/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#if defined(ENABLE_MP4)

#include "MP4.h"
#include "Util/File.h"
#include "Util/logger.h"
#include "Common/config.h"

using namespace toolkit;
using namespace std;

namespace mediakit {
//////////////////////////////////////////////////
// copy from mp4-writer.h and add webm mkv support 
//////////////////////////////////////////////////
/// @param[in] flags mov flags, such as: MOV_FLAG_SEGMENT, see more @mov-format.h
struct mp4_writer_t* mp4_writer_create(int type, const struct mov_buffer_t* buffer, void* param, int flags) {
    struct mp4_writer_t* mp4;
    mp4 = (struct mp4_writer_t*)calloc(1, sizeof(struct mp4_writer_t));
    if (!mp4) return NULL;
    memset(mp4, 0, sizeof(*mp4));
    switch (type) {
    case 1:
        mp4->fmp4 = fmp4_writer_create(buffer, param, flags);
        break;
    case 0:
        mp4->mov = mov_writer_create(buffer, param, flags); 
        break;
    case 2:
        mp4->mkv = mkv_writer_create((const struct mkv_buffer_t*)buffer, param, flags);
        break;
    default:
        break;
    }
    return mp4;
}

void mp4_writer_destroy(struct mp4_writer_t* mp4) {
    if (mp4->mov) {
        mov_writer_destroy(mp4->mov); 
    } 
    if (mp4->fmp4) {
        fmp4_writer_destroy(mp4->fmp4);
    }
    if (mp4->mkv) {
        mkv_writer_destroy(mp4->mkv);
    }
    free(mp4);
}

/// @param[in] object MPEG-4 systems ObjectTypeIndication such as: MOV_OBJECT_AAC, see more @mov-format.h
/// @param[in] extra_data AudioSpecificConfig
/// @return >=0-track, <0-error
int mp4_writer_add_audio(struct mp4_writer_t* mp4, CodecId object, int channel_count, int bits_per_sample, int sample_rate, const void* extra_data, size_t extra_data_size) {
    if (mp4->mov) {
        return mov_writer_add_audio(mp4->mov, getMovIdByCodec(object), channel_count, bits_per_sample, sample_rate, extra_data, extra_data_size);
    }
    if (mp4->fmp4) {
        return fmp4_writer_add_audio(mp4->fmp4, getMovIdByCodec(object), channel_count, bits_per_sample, sample_rate, extra_data, extra_data_size);
    }
    if (mp4->mkv) {
        return mkv_writer_add_audio(mp4->mkv, (mkv_codec_t)getMkvIdByCodec(object), channel_count, bits_per_sample, sample_rate, extra_data, extra_data_size);
    }
    return -1;
}

/// @param[in] object MPEG-4 systems ObjectTypeIndication such as: MOV_OBJECT_H264, see more @mov-format.h
/// @param[in] extra_data AVCDecoderConfigurationRecord/HEVCDecoderConfigurationRecord
/// @return >=0-track, <0-error
int mp4_writer_add_video(struct mp4_writer_t* mp4, CodecId object, int width, int height, const void* extra_data, size_t extra_data_size) {
    if (mp4->mov) {
        return mov_writer_add_video(mp4->mov, getMovIdByCodec(object), width, height, extra_data, extra_data_size);
    } 
    if (mp4->fmp4) {
        return fmp4_writer_add_video(mp4->fmp4, getMovIdByCodec(object), width, height, extra_data, extra_data_size);
    }
    if (mp4->mkv) {
        return mkv_writer_add_video(mp4->mkv, (mkv_codec_t)getMkvIdByCodec(object), width, height, extra_data, extra_data_size);
    }
    return -1;
}

int mp4_writer_add_subtitle(struct mp4_writer_t* mp4, CodecId object, const void* extra_data, size_t extra_data_size) {
    if (mp4->mov) {
        return mov_writer_add_subtitle(mp4->mov, getMovIdByCodec(object), extra_data, extra_data_size);
    } 
    if (mp4->fmp4) {
        return fmp4_writer_add_subtitle(mp4->fmp4, getMovIdByCodec(object), extra_data, extra_data_size);
    }
    if (mp4->mkv) {
        return mkv_writer_add_subtitle(mp4->mkv, (mkv_codec_t)getMkvIdByCodec(object), extra_data, extra_data_size);
    }
    return -1;
}

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
int mp4_writer_write(struct mp4_writer_t* mp4, int track, const void* data, size_t bytes, int64_t pts, int64_t dts, int flags)
{
    if (mp4->mov) {
        return mov_writer_write(mp4->mov, track, data, bytes, pts, dts, flags);
    } 
    if (mp4->fmp4) {
        return fmp4_writer_write(mp4->fmp4, track, data, bytes, pts, dts, flags);
    }
    if (mp4->mkv) {
        return mkv_writer_write(mp4->mkv, track, data, bytes, pts, dts, flags);
    }
    return -1;
}

///////////////////// The following interfaces are only applicable to fmp4 ///////////////////////////////

/// Save data and open next segment
/// @return 0-ok, other-error
int mp4_writer_save_segment(struct mp4_writer_t* mp4) {
    if (mp4->fmp4)
        return fmp4_writer_save_segment(mp4->fmp4);
    return 0;
}

/// Get init segment data(write FTYP, MOOV only)
/// WARNING: it caller duty to switch file/buffer context with fmp4_writer_write
/// @return 0-ok, other-error
int mp4_writer_init_segment(struct mp4_writer_t* mp4) {
    if (mp4->fmp4)
        return fmp4_writer_init_segment(mp4->fmp4);
    return 0;
}

static struct mov_buffer_t s_io = {
        [](void *ctx, void *data, uint64_t bytes) {
            MP4FileIO *thiz = (MP4FileIO *) ctx;
            return thiz->onRead(data, bytes);
        },
        [](void *ctx, const void *data, uint64_t bytes) {
            MP4FileIO *thiz = (MP4FileIO *) ctx;
            return thiz->onWrite(data, bytes);
        },
        [](void *ctx, int64_t offset) {
            MP4FileIO *thiz = (MP4FileIO *) ctx;
            return thiz->onSeek(offset);
        },
        [](void *ctx) {
            MP4FileIO *thiz = (MP4FileIO *) ctx;
            return thiz->onTell();
        }
};

static struct mkv_buffer_t w_io = {
        [](void *ctx, void *data, uint64_t bytes) {
            MP4FileIO *thiz = (MP4FileIO *) ctx;
            return thiz->onRead(data, bytes);
        },
        [](void *ctx, const void *data, uint64_t bytes) {
            MP4FileIO *thiz = (MP4FileIO *) ctx;
            return thiz->onWrite(data, bytes);
        },
        [](void *ctx, int64_t offset) {
            MP4FileIO *thiz = (MP4FileIO *) ctx;
            return thiz->onSeek(offset);
        },
        [](void *ctx) {
            MP4FileIO *thiz = (MP4FileIO *) ctx;
            return (int64_t)thiz->onTell();
        }
};

MP4FileIO::Writer MP4FileIO::createWriter(int flags, int type){
    Writer writer;
    Ptr self = shared_from_this();
    // 保存自己的强引用，防止提前释放  [AUTO-TRANSLATED:e8e14f60]
    // Save a strong reference to itself to prevent premature release
    writer.reset(mp4_writer_create(type, &s_io,this, flags),[self](mp4_writer_t *ptr){
        if(ptr){
            mp4_writer_destroy(ptr);
        }
    });
    if(!writer){
        throw std::runtime_error("写入mp4文件失败!");
    }
    return writer;
}

MP4FileIO::Reader MP4FileIO::createReader(){
    Reader reader;
    Ptr self = shared_from_this();
    // 保存自己的强引用，防止提前释放  [AUTO-TRANSLATED:e8e14f60]
    // Save a strong reference to itself to prevent premature release
    reader.reset(mov_reader_create(&s_io,this),[self](mov_reader_t *ptr){
        if(ptr){
            mov_reader_destroy(ptr);
        }
    });
    if(!reader){
        throw std::runtime_error("读取mp4文件失败!");
    }
    return reader;
}

MP4FileIO::WebmReader MP4FileIO::createWebmReader() {
    WebmReader reader;
    Ptr self = shared_from_this();
    // 保存自己的强引用，防止提前释放  [AUTO-TRANSLATED:e8e14f60]
    // Save a strong reference to itself to prevent premature release
    reader.reset(mkv_reader_create(&w_io, this), [self](mkv_reader_t *ptr) {
        if (ptr) {
            mkv_reader_destroy(ptr);
        }
    });
    if (!reader) {
        throw std::runtime_error("读取Webm文件失败!");
    }
    return reader;
}

/////////////////////////////////////////////////////MP4FileDisk/////////////////////////////////////////////////////////

#if defined(_WIN32) || defined(_WIN64)
    #define fseek64 _fseeki64
    #define ftell64 _ftelli64
#else
    #define fseek64 fseek
    #define ftell64 ftell
#endif

void MP4FileDisk::openFile(const char *file, const char *mode) {
    // 创建文件  [AUTO-TRANSLATED:bd145ed5]
    // Create a file
    auto fp = File::create_file(file, mode);
    if(!fp){
        throw std::runtime_error(string("打开文件失败:") + file);
    }

    GET_CONFIG(uint32_t,mp4BufSize,Record::kFileBufSize);

    // 新建文件io缓存  [AUTO-TRANSLATED:fda9ff47]
    // Create a new file io cache
    std::shared_ptr<char> file_buf(new char[mp4BufSize],[](char *ptr){
        if(ptr){
            delete [] ptr;
        }
    });

    if(file_buf){
        // 设置文件io缓存  [AUTO-TRANSLATED:0ed9c8ad]
        // Set the file io cache
        setvbuf(fp, file_buf.get(), _IOFBF, mp4BufSize);
    }

    // 创建智能指针  [AUTO-TRANSLATED:e7920ab2]
    // Create a smart pointer
    _file.reset(fp,[file_buf](FILE *fp) {
        fflush(fp);
        fclose(fp);
    });
}

void MP4FileDisk::closeFile() {
    _file = nullptr;
}

int MP4FileDisk::onRead(void *data, size_t bytes) {
    if (bytes == fread(data, 1, bytes, _file.get())){
        return 0;
    }
    return 0 != ferror(_file.get()) ? ferror(_file.get()) : -1 /*EOF*/;
}

int MP4FileDisk::onWrite(const void *data, size_t bytes) {
    return bytes == fwrite(data, 1, bytes, _file.get()) ? 0 : ferror(_file.get());
}

int MP4FileDisk::onSeek(int64_t offset) {
    return fseek64(_file.get(), offset, offset >= 0 ? SEEK_SET : SEEK_END);
}

int64_t MP4FileDisk::onTell() {
    return ftell64(_file.get());
}

/////////////////////////////////////////////////////MP4FileMemory/////////////////////////////////////////////////////////

string MP4FileMemory::getAndClearMemory(){
    string ret;
    ret.swap(_memory);
    _offset = 0;
    return ret;
}

size_t MP4FileMemory::fileSize() const{
    return _memory.size();
}

int64_t MP4FileMemory::onTell(){
    return _offset;
}

int MP4FileMemory::onSeek(int64_t offset){
    if (offset < 0) {
        offset += _memory.size();
        if (offset < 0) {
            return -1;
        }
        _offset = offset;
    } else {
        if (offset > _memory.size()) {
            return -1;
        }
        _offset = offset;
    }
    return 0;
}

int MP4FileMemory::onRead(void *data, size_t bytes){
    if (_offset >= _memory.size()) {
        //EOF
        return -1;
    }
    bytes = MIN(bytes, _memory.size() - _offset);
    memcpy(data, _memory.data() + _offset, bytes);
    _offset += bytes;
    return 0;
}

int MP4FileMemory::onWrite(const void *data, size_t bytes){
    if (_offset + bytes > _memory.size()) {
        // 需要扩容  [AUTO-TRANSLATED:211c91e3]
        // Need to expand
        _memory.resize(_offset + bytes);
    }
    memcpy((uint8_t *) _memory.data() + _offset, data, bytes);
    _offset += bytes;
    return 0;
}

}//namespace mediakit
#endif // defined(ENABLE_MP4)
