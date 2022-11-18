#include "SpeedStatistic.h"

namespace toolkit {

BytesSpeed & BytesSpeed::operator+=(size_t bytes)
{
    _bytes += bytes;
    if (_bytes > 1024 * 1024) {
        //数据大于1MB就计算一次网速
        computeSpeed();
    }
    return *this;
}

int BytesSpeed::getSpeed()
{
    if (_ticker.elapsedTime() < 1000) {
        //获取频率小于1秒，那么返回上次计算结果
        return _speed;
    }
    return computeSpeed();
}

int BytesSpeed::computeSpeed()
{
    auto elapsed = _ticker.elapsedTime();
    if (!elapsed) {
        return _speed;
    }
    _speed = (int)(_bytes * 1000 / elapsed);
    _ticker.resetTime();
    _bytes = 0;
    return _speed;
}

}


