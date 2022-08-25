/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */
#include "Util/logger.h"
#include "TSDecoder.h"
namespace mediakit {

bool TSSegment::isTSPacket(const char *data, size_t len){
    return len == TS_PACKET_SIZE && ((uint8_t*)data)[0] == TS_SYNC_BYTE;
}

void TSSegment::setOnSegment(TSSegment::onSegment cb) {
    _onSegment = std::move(cb);
}

ssize_t TSSegment::onRecvHeader(const char *data, size_t len) {
    if (!isTSPacket(data, len)) {
        WarnL << "不是ts包:" << (int) (data[0]) << " " << len;
    }
    else {
        _onSegment(data, len);
    }
    // 返回0，则不用Content-lenght来找帧
    return 0;
}

const char *TSSegment::onSearchPacketTail(const char *data, size_t len) {
    if (len < _size + 1) {
        if (len == _size && data[0] == TS_SYNC_BYTE) {
            return data + _size;
        }
        return nullptr;
    }
    //下一个包头
    if (data[_size] == TS_SYNC_BYTE) {
        return data + _size;
    }
    // 通过包头来定位起点
    auto pos = memchr(data + _size, TS_SYNC_BYTE, len - _size);
    if (pos) {
        return (char *) pos;
    }
    if (remainDataSize() > 4 * _size) {
        //这么多数据都没ts包，则清空
        return data + len;
    }
    //等待更多数据
    return nullptr;
}

////////////////////////////////////////////////////////////////

#if defined(ENABLE_HLS)
#include "ts_demux.hpp"
TSDecoder::TSDecoder() : _ts_segment() {
    _demuxer_ctx.reset(new ts_demux());

    _ts_segment.setOnSegment([this](const char *data, size_t len) {
        auto buf = toolkit::BufferRaw::create();
        buf->assign(data, len);
        _demuxer_ctx->decode(buf, shared_from_this());
    });
}

TSDecoder::~TSDecoder() {
}

ssize_t TSDecoder::input(const uint8_t *data, size_t bytes) {
    if (TSSegment::isTSPacket((char *)data, bytes)) {
        auto buf = toolkit::BufferRaw::create();
        buf->assign((const char*)data, bytes);
        return _demuxer_ctx->decode(buf, shared_from_this());
    }
    try {
        _ts_segment.input((char *) data, bytes);
    } catch (...) {
        //ts解析失败，清空缓存数据
        _ts_segment.reset();
        throw;
    }
    return bytes;
}

int TSDecoder::on_data_callback(SRT_DATA_MSG_PTR data_ptr, unsigned int media_type, uint64_t dts, uint64_t pts)
{
    auto it = _type_map.find(media_type);
    if (it==_type_map.end()) {
        this->_on_stream(_type_map.size(), media_type, nullptr, 0, 0);
        it = _type_map.emplace(std::make_pair(media_type, _type_map.size())).first;
    }
    this->_on_decode(it->first, media_type, 0, pts, dts, data_ptr->data(), data_ptr->size());
    return 0;
}

#endif//defined(ENABLE_HLS)

}//namespace mediakit
