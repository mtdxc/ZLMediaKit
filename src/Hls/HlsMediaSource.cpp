/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "HlsMediaSource.h"
#include "Common/config.h"
#include "Util/logger.h"
#include "EventLoopThreadPool.h"
#include "Session.h"
using namespace toolkit;

namespace mediakit {

HlsCookieData::HlsCookieData(const MediaInfo &info, const std::shared_ptr<SockInfo> &sock_info) {
    _info = info;
    _sock_info = sock_info;
    _added = std::make_shared<bool>(false);
    addReaderCount();
}

void HlsCookieData::addReaderCount() {
    if (!*_added) {
        auto src = getMediaSource();
        if (src) {
            auto added = _added;
            *added = true;
            _ring_reader = src->getRing()->attach(hv::EventLoopThreadPool::Instance()->loop());
            _ring_reader->setDetachCB([added]() {
                // HlsMediaSource已经销毁
                *added = false;
            });
        }
    }
}

HlsCookieData::~HlsCookieData() {
    if (*_added) {
        uint64_t duration = (_ticker.createdTime() - _ticker.elapsedTime()) / 1000;
        WarnP(_sock_info)  << "HLS播放器(" << _info.shortUrl() << ")断开,耗时(s):" << duration;

        GET_CONFIG(uint32_t, iFlowThreshold, General::kFlowThreshold);
        uint64_t bytes = _bytes.load();
        if (bytes >= iFlowThreshold * 1024) {
            NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastFlowReport, _info, bytes, duration, true, static_cast<SockInfo &>(*_sock_info));
        }
    }
}

void HlsCookieData::addByteUsage(size_t bytes) {
    addReaderCount();
    _bytes += bytes;
    _ticker.resetTime();
}

void HlsMediaSource::setIndexFile(std::string index_file)
{
    if (!_ring) {
        std::weak_ptr<HlsMediaSource> weakSelf = std::dynamic_pointer_cast<HlsMediaSource>(shared_from_this());
        _ring = std::make_shared<RingType>(0, [weakSelf](int size) {
            if (auto strongSelf = weakSelf.lock())
                strongSelf->onReaderChanged(size);
            });
        regist();
    }

    //赋值m3u8索引文件内容
    std::lock_guard<std::mutex> lck(_mtx_index);
    _index_file = std::move(index_file);

    if (!_index_file.empty()) {
        for (auto cb : _list_cb) { cb(_index_file); }
        _list_cb.clear();
    }
}

void HlsMediaSource::getIndexFile(std::function<void(const std::string& str)> cb)
{
    std::lock_guard<std::mutex> lck(_mtx_index);
    if (!_index_file.empty()) {
        cb(_index_file);
    }
    else {
        // 等待生成m3u8文件
        _list_cb.emplace_back(std::move(cb));
    }
}

std::string HlsMediaSource::getIndexFile() const
{
    std::lock_guard<std::mutex> lck(_mtx_index);
    return _index_file;
}

} // namespace mediakit
