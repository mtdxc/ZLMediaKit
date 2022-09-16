/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef HLSMAKER_H
#define HLSMAKER_H

#include <deque>
#include <string>
#include <stdint.h>

namespace mediakit {

class HlsMaker {
public:
    /**
     * @param seg_duration 切片文件长度
     * @param seg_number 切片个数
     * @param seg_keep 是否保留切片文件
     */
    HlsMaker(float seg_duration = 5, uint32_t seg_number = 3, bool seg_keep = false);
    virtual ~HlsMaker() {}

    /**
     * 写入ts数据
     * @param data 数据
     * @param len 数据长度
     * @param timestamp 毫秒时间戳
     * @param is_idr_fast_packet 是否为关键帧第一个包
     */
    void inputData(void *data, size_t len, uint64_t timestamp, bool is_idr_fast_packet);

    /**
     * 是否为直播
     */
    bool isLive() {
        return _seg_number != 0;
    }

    /**
     * 是否保留切片文件
     */
    bool isKeep() {return _seg_keep;}

    /**
     * 清空记录
     * 这会导致重新等关键帧才产生下一个切片
     */
    void clear();

    /**
     * 创建ts切片文件回调
     * @param index 索引
     * @return 新的切片文件名
     */
    virtual std::string onOpenSegment(uint64_t index) = 0;

    /**
     * 删除ts切片文件回调
     * @param index 索引
     */
    virtual void onDelSegment(uint64_t index) = 0;

    /**
     * 当前ts文件写通知
     * @param data
     * @param len
     */
    virtual void onWriteSegment(const char *data, size_t len) = 0;

    /**
     * 写m3u8文件回调
     */
    virtual void onWriteHls(const std::string &data) = 0;

    /**
     * 当前ts切片写完通知
     * @param duration_ms 上一个 ts 切片的时长, 单位为毫秒
     */
    virtual void onFlushLastSegment(uint64_t duration_ms) {};

    /**
     * 关闭上个ts切片, 并更新m3u8索引
     * @param eof HLS直播是否已结束
     * - add TsItem
     * - delOldSegment
     * - onFlushLastSegment
     * - makeIndexFile(eof)
     *   - onWriteHls
     */
    void flushLastSegment(bool eof);

private:
    /**
     * 生成m3u8文件
     * @param eof true代表点播
     */
    void makeIndexFile(bool eof = false);

    /**
     * 删除旧的ts切片
     */
    void delOldSegment();

    /**
     * 必要时创建新的ts切片,一般在收到关键帧时调用
     * @param timestamp
     * - flushLastSegment
     * - onOpenSegment(_file_index++) -> _last_file_name
     * - update _last_seg_timestamp
     */
    void addNewSegment(uint64_t timestamp);

protected:
    uint64_t current_index() const {
        if (_file_index)
            return _file_index - 1;
        return 0;
    }

    // 单个切片的最大时长s(超过后条件满足就会产生新切片)
    float _seg_duration = 0;
    // 0 点播模式, > 0 live模式(只保留_seg_number个最新的segment)
    uint32_t _seg_number = 0;
    // 不删除ts文件
    bool _seg_keep = false;
    // 当前时间戳
    uint64_t _last_timestamp = 0;
    // 最后切片的开始时间戳, 它减去_last_timestamp就是当前切片时长
    uint64_t _last_seg_timestamp = 0;
    // 切片索引号，从0开始，不断+1
    uint64_t _file_index = 0;
    // 当前切片文件名
    std::string _last_file_name;
    struct TsItem {
        int duration;
        std::string filename;
    };
    // 索引 + 文件名 列表，用于生成m3u8文件
    std::deque<TsItem> _seg_dur_list;
};

}//namespace mediakit
#endif //HLSMAKER_H
