/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <algorithm>
#include "MediaPusher.h"
#include "PusherBase.h"
#include "Common/MediaSource.h"
#include "EventLoopThreadPool.h"

using std::string;
using namespace toolkit;

namespace mediakit {

MediaPusher::MediaPusher(const MediaSource::Ptr &src,
                         const EventPoller::Ptr &poller) {
    _src = src;
    _poller = poller ? poller : hv::EventLoopThreadPool::Instance()->loop();
}

MediaPusher::MediaPusher(const string &schema,
                         const string &vhost,
                         const string &app,
                         const string &stream,
                         const EventPoller::Ptr &poller) :
        MediaPusher(MediaSource::find(schema, vhost, app, stream), poller){
}

MediaPusher::~MediaPusher() {
}

void MediaPusher::publish(const string &url) {
    _delegate = PusherBase::createPusher(_poller, _src.lock(), url);
    assert(_delegate);
    _delegate->setOnShutdown(_on_shutdown);
    _delegate->setOnPublished(_on_publish);
    _delegate->mINI::operator=(*this);
    _delegate->publish(url);
}

EventPoller::Ptr MediaPusher::getPoller(){
    return _poller;
}

} /* namespace mediakit */
