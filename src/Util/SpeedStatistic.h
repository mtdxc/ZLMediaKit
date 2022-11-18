/*
* Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
*
* This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
*
* Use of this source code is governed by MIT license that can be found in the
* LICENSE file in the root of the source tree. All contributing project authors
* may be found in the AUTHORS file in the root of the source tree.
*/

#ifndef SPEED_STATISTIC_H_
#define SPEED_STATISTIC_H_
#include <cstddef>
#include "TimeTicker.h"

namespace toolkit {

class BytesSpeed {
public:
    BytesSpeed() = default;
    ~BytesSpeed() = default;

    /**
     * 添加统计字节
     */
    BytesSpeed &operator+=(size_t bytes);

    /**
     * 获取速度，单位bytes/s
     */
    int getSpeed();

private:
    int computeSpeed();

private:
    int _speed = 0;
    size_t _bytes = 0;
    Ticker _ticker;
};

} /* namespace toolkit */
#endif /* SPEED_STATISTIC_H_ */
