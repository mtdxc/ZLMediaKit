/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SRC_PUSHER_MEDIAPUSHER_H_
#define SRC_PUSHER_MEDIAPUSHER_H_

#include <memory>
#include <string>
#include "PusherBase.h"
//#include "Thread/TaskExecutor.h"

namespace mediakit {

class MediaPusher : public PusherImp<PusherBase,PusherBase> {
public:
    typedef std::shared_ptr<MediaPusher> Ptr;

    MediaPusher(const std::string &schema,
                const std::string &vhost,
                const std::string &app,
                const std::string &stream,
                const toolkit::EventPollerPtr &poller = nullptr);

    MediaPusher(const std::shared_ptr<MediaSource> &src,
                const toolkit::EventPollerPtr &poller = nullptr);

    virtual ~MediaPusher();

    void publish(const std::string &url) override;
    toolkit::EventPollerPtr getPoller();

private:
    std::weak_ptr<MediaSource> _src;
    toolkit::EventPollerPtr _poller;
};

} /* namespace mediakit */

#endif /* SRC_PUSHER_MEDIAPUSHER_H_ */
