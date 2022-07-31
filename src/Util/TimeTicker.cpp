#include "TimeTicker.h"
#include "util.h"
using namespace toolkit;

Ticker::Ticker(uint64_t min_ms /*= 0*/)
{
    _created = _begin = getCurrentMillisecond();
    _min_ms = min_ms;
}

Ticker::~Ticker()
{
    uint64_t tm = createdTime();
    /*
    if (tm > _min_ms) {
        _ctx << "tooks " << tm << " ms, more time used";
    } else {
        _ctx.clear();
    }*/
}

uint64_t Ticker::createdTime() const
{
    return getCurrentMillisecond() - _created;
}

void Ticker::resetTime()
{
    _begin = getCurrentMillisecond();
}

uint64_t Ticker::elapsedTime() const
{
    return getCurrentMillisecond() - _begin;
}

SmoothTicker::SmoothTicker(uint64_t reset_ms /*= 10000*/)
{
    _reset_ms = reset_ms;
    _ticker.resetTime();
}

SmoothTicker::~SmoothTicker()
{

}

uint64_t SmoothTicker::elapsedTime()
{
    auto now_time = _ticker.elapsedTime();
    if (_first_time == 0) {
        if (now_time < _last_time) {
            auto last_time = _last_time - _time_inc;
            double elapse_time = (now_time - last_time);
            _time_inc += (elapse_time / ++_pkt_count) / 3;
            auto ret_time = last_time + _time_inc;
            _last_time = (uint64_t)ret_time;
            return (uint64_t)ret_time;
        }
        _first_time = now_time;
        _last_time = now_time;
        _pkt_count = 0;
        _time_inc = 0;
        return now_time;
    }

    auto elapse_time = (now_time - _first_time);
    _time_inc += elapse_time / ++_pkt_count;
    auto ret_time = _first_time + _time_inc;
    if (elapse_time > _reset_ms) {
        _first_time = 0;
    }
    _last_time = (uint64_t)ret_time;
    return (uint64_t)ret_time;
}

void SmoothTicker::resetTime()
{
    _first_time = 0;
    _pkt_count = 0;
    _ticker.resetTime();
}
