/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Opus.h"

namespace mediakit{

Track::Ptr OpusTrack::clone() {
    return std::make_shared<std::remove_reference<decltype(*this)>::type >(*this);
}

}//namespace mediakit