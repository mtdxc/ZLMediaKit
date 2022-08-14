/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_TSDECODER_H
#define ZLMEDIAKIT_TSDECODER_H

#include "Util/HttpRequestSplitter.h"
#include "Decoder.h"
#if defined(ENABLE_HLS)
#include "ts_demux.hpp"
#endif
#define TS_PACKET_SIZE		188
#define TS_SYNC_BYTE        0x47

namespace mediakit {

//TS包分割器，用于split一个一个的ts包
class TSSegment : public HttpRequestSplitter {
public:
    TSSegment(size_t size = TS_PACKET_SIZE) : _size(size){}
    ~TSSegment(){}

    // 收到TS包回调
    typedef std::function<void(const char *data, size_t len)> onSegment;
    void setOnSegment(onSegment cb);

    static bool isTSPacket(const char *data, size_t len);

protected:
    ssize_t onRecvHeader(const char *data, size_t len) override ;
    const char *onSearchPacketTail(const char *data, size_t len) override ;

private:
    const size_t _size;
    onSegment _onSegment;
};

#if defined(ENABLE_HLS)
//ts解析器
class TSDecoder : public Decoder, 
    public ts_media_data_callback_I, public std::enable_shared_from_this<TSDecoder> {
public:
    TSDecoder();
    ~TSDecoder();

    ssize_t input(const uint8_t* data, size_t bytes) override ;

private:
    std::map<int, int> _type_map;
    virtual int on_data_callback(SRT_DATA_MSG_PTR data_ptr, unsigned int media_type, uint64_t dts, uint64_t pts) override;
    TSSegment _ts_segment;
    std::shared_ptr<ts_demux> _demuxer_ctx;
};
#endif//defined(ENABLE_HLS)

}//namespace mediakit
#endif //ZLMEDIAKIT_TSDECODER_H
