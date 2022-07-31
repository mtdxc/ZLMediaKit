/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "util.h"
#include "NoticeCenter.h"

namespace toolkit {

INSTANCE_IMP(NoticeCenter)

void NoticeCenter::delListener(void *tag)
{
    std::lock_guard<std::recursive_mutex> lck(_mtxListener);
    bool empty;
    for (auto it = _mapListener.begin(); it != _mapListener.end();) {
        it->second->delListener(tag, empty);
        if (empty) {
            it = _mapListener.erase(it);
            continue;
        }
        ++it;
    }
}

void NoticeCenter::delListener(void *tag, const std::string &event)
{
    auto dispatcher = getDispatcher(event);
    if (!dispatcher) {
        //不存在该事件
        return;
    }
    bool empty;
    dispatcher->delListener(tag, empty);
    if (empty) {
        delDispatcher(event, dispatcher);
    }
}

void NoticeCenter::clearAll()
{
    std::lock_guard<std::recursive_mutex> lck(_mtxListener);
    _mapListener.clear();
}

EventDispatcher::Ptr NoticeCenter::getDispatcher(const std::string &event, bool create /*= false*/)
{
    std::lock_guard<std::recursive_mutex> lck(_mtxListener);
    auto it = _mapListener.find(event);
    if (it != _mapListener.end()) {
        return it->second;
    }
    if (create) {
        //如果为空则创建一个
        EventDispatcher::Ptr dispatcher(new EventDispatcher());
        _mapListener.emplace(event, dispatcher);
        return dispatcher;
    }
    return nullptr;
}

void NoticeCenter::delDispatcher(const std::string &event, const EventDispatcher::Ptr &dispatcher)
{
    std::lock_guard<std::recursive_mutex> lck(_mtxListener);
    auto it = _mapListener.find(event);
    if (it != _mapListener.end() && dispatcher == it->second) {
        //两者相同则删除
        _mapListener.erase(it);
    }
}

} /* namespace toolkit */

