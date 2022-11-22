/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef HLSMAKERIMP_H
#define HLSMAKERIMP_H

#include <memory>
#include <string>
#include <stdlib.h>
#include <list>
#include <map>
#include "Record/Recorder.h"
#include "HlsMaker.h"
#include "toolkit.h"

namespace mediakit {
class HlsMediaSource;
typedef std::function<void (bool, std::string)> SnapCB;
// 负责将多个TS文件合并成一个大ts文件
struct SnapTask {
    typedef std::shared_ptr<SnapTask> Ptr;
    // 目的ts文件
    std::string dstFile;
    // 源TS文件列表
    std::list<std::string> files;
    // 完成回调
	SnapCB cb;
    /*
    Task状态
    0  进行中
    >0 成功
    <0 失败
    */
    int status = 0;
    std::string error;
    void Reset() {status = 0;}
    // 工作函数：文件合并，并回调结果
    void DoTask();
    // 回调结果并设置status
    void Notify(int code, const char* err);
};

class HlsMakerImp : public HlsMaker{
public:
    HlsMakerImp(const std::string &m3u8_file,
                const std::string &params,
                uint32_t bufSize  = 64 * 1024,
                float seg_duration = 5,
                uint32_t seg_number = 3,
                bool seg_keep = false);

    ~HlsMakerImp() override;

    /**
     * 设置媒体信息
     * @param vhost 虚拟主机
     * @param app 应用名
     * @param stream_id 流id
     */
    void setMediaSource(const std::string &vhost, const std::string &app, const std::string &stream_id);

    /**
     * 获取MediaSource
     * @return
     */
    std::shared_ptr<HlsMediaSource> getMediaSource() const;

    /**
     * 清空缓存
     */
    void clearCache();

    // 新建一个多少s的快照
    void makeSnap(int maxSec, const std::string& prefix, SnapCB cb);
    // 获取本地缓存长度
    float duration() const { return _duration + (_last_timestamp - _last_seg_timestamp)/1000.0f; }
protected:
    bool lookupFile(uint64_t index, std::string& file);
    bool parseFileName(std::string seg_path, time_t& time, int64_t* index);

    std::string onOpenSegment(uint64_t index) override;
    void onDelSegment(uint64_t index) override;
    void onWriteSegment(const char *data, size_t len) override;
    void onWriteHls(const std::string &data) override;
    void onFlushLastSegment(uint64_t duration_ms) override;

private:
    std::shared_ptr<FILE> makeFile(const std::string &file,bool setbuf = false);
    void clearCache(bool immediately, bool eof);

private:
    int _buf_size;
    std::string _params;
    std::string _path_hls;
    std::string _path_prefix;
    RecordInfo _info;
    std::shared_ptr<FILE> _file;
    std::shared_ptr<char> _file_buf;
    std::shared_ptr<HlsMediaSource> _media_src;
    toolkit::EventPollerPtr _poller;
    std::map<uint64_t/*index*/, TsItem> _segment_file_paths;
    float _duration = 0;
};

}//namespace mediakit
#endif //HLSMAKERIMP_H
