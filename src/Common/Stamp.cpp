/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Stamp.h"
#include "Common/config.h"

#define ABS(x) ((x) > 0 ? (x) : (-x))

using namespace toolkit;
#define kSynCount "stamp.sync_count"
#define kMaxDelta "stamp.max_delta"
#define kMaxCts "stamp.max_cts"
#define kMaxNtpDelta "ntp.max_delta"
#define kNtpLoopDelta "ntp.loop_delta"
static onceToken token([]() {
    mINI::Instance()[kSynCount] = 0; // 之前是3, 这里改成0
    mINI::Instance()[kMaxDelta] = 300;
    mINI::Instance()[kMaxCts] = 500;
    // Timestamp maximum allowable jump is 3 seconds, mainly to prevent network jitter caused by the jump
    mINI::Instance()[kMaxNtpDelta] = (3 * 1000);
    mINI::Instance()[kNtpLoopDelta] = (60 * 1000);
});

namespace mediakit {

DeltaStamp::DeltaStamp() {
    // 时间戳最大允许跳跃300ms
    _max_delta = mINI::Instance()[kMaxDelta];
}

void DeltaStamp::reset() {
    _last_stamp = 0;
    _relative_stamp = 0;
    _last_delta = 1;
}

int64_t DeltaStamp::deltaStamp(int64_t stamp, bool enable_rollback) {
    if (!_last_stamp) {
        // 第一次计算时间戳增量,时间戳增量为0
        if (stamp) {
            _last_stamp = stamp;
        }
        return 0;
    }

    int64_t ret = stamp - _last_stamp;
    if (ret >= 0) {
        _last_stamp = stamp;
        // 在直播情况下，当时间戳增量太大时，则当成时间戳跳变，返回前一个时间戳增量(当前为1)
        if (ret > _max_delta) {
            needSync();
            return _last_delta;
        }
        _last_delta = ret;
        return ret;
    }

    // 时间戳增量为负，说明时间戳回环了或回退了
    _last_stamp = stamp;
    if (!enable_rollback || -ret > _max_delta) {
        // 不允许回退或者回退太多了, 强制时间戳加前一增量(当前为1)
        needSync();
        return _last_delta;
    }
    return ret;
}

void DeltaStamp::setMaxDelta(size_t max_delta) {
    if (_max_delta == max_delta) {
        return;
    }
    InfoL << _max_delta << " -> " << max_delta;
    _max_delta = max_delta;
}

////////////////////////////
// Stamp实现
void Stamp::reset() {
    DeltaStamp::reset();
    _last_dts_in = 0;
    _last_dts_out = 0;
    _last_pts_out = 0;
}

// 在revise_l上，增加限制dts回退功能
void Stamp::revise(int64_t dts, int64_t pts, int64_t &dts_out, int64_t &pts_out, bool modifyStamp) {
    revise_l(dts, pts, dts_out, pts_out, modifyStamp);
    if (_playback) {
        // Playback allows timestamp rollback
        return;
    }

    if (dts_out < _last_dts_out) {
        // WarnL << "dts rollback:" << dts_out << " < " << _last_dts_out;
        dts_out = _last_dts_out;
        pts_out = _last_pts_out;
        return;
    }
    _last_dts_out = dts_out;
    _last_pts_out = pts_out;
}

// 在revise_l2上，增加音视频时间戳同步
void Stamp::revise_l(int64_t dts, int64_t pts, int64_t &dts_out, int64_t &pts_out, bool modifyStamp) {
    revise_l2(dts, pts, dts_out, pts_out, modifyStamp);
    if (!_sync_master || modifyStamp || _playback) {
        // 自动生成时间戳或回放或同步完毕
        return;
    }
    GET_CONFIG_FUNC(uint32_t, sync_count, kSynCount, [](const std::string& val) {
        int ret = std::stoi(val);
        if (ret < 0) ret = 0;
        return ret;
    });
    // 需要同步时间戳
    if (_sync_master && _sync_master->_last_dts_in && (_need_sync || _sync_master->_need_sync)) {
        // 音视频dts当前时间差
        int64_t dts_diff = _last_dts_in - _sync_master->_last_dts_in;
        if (ABS(dts_diff) < 5000 || _need_sync > sync_count) {
            // 两种时间戳相差不得大于300ms
            dts_diff = _relative_stamp - _sync_master->_relative_stamp;
            // 强制同步音视频
            if (dts_diff > _max_delta) {
                dts_diff = 0;
            } else if (dts_diff < -_max_delta) {
                dts_diff = 0;
            }
            // 如果绝对时间戳小于5秒，那么说明他们的起始时间戳是一致的，那么强制同步
            auto target_stamp = _sync_master->_relative_stamp + dts_diff;
            if (target_stamp > _relative_stamp || _enable_rollback) {
                // 同步后，时间戳增加跳跃了，或允许回退
                if (_relative_stamp == target_stamp) {
                    return;
                }
                TraceL << "Self Relative stamp changed: " << _relative_stamp << " -> " << target_stamp;
                _relative_stamp = target_stamp;
                // 以新的相对时间戳生成此帧
                revise_l2(dts, pts, dts_out, pts_out, modifyStamp);
            } else {
                // 不允许回退, 则让另外一个Track的时间戳增长
                target_stamp = _relative_stamp - dts_diff;
                if (_sync_master->_relative_stamp == target_stamp) {
                    return;
                }
                TraceL << "Target Relative stamp changed: " << _sync_master->_relative_stamp << " -> " << target_stamp;
                _sync_master->_relative_stamp = target_stamp;
            }
        }
        // 减少同步计数
        if (_need_sync) {
            --_need_sync;
        }
        if (_sync_master->_need_sync) {
            --_sync_master->_need_sync;
        }
    }
}

// Obtain the relative timestamp
void Stamp::revise_l2(int64_t dts, int64_t pts, int64_t &dts_out, int64_t &pts_out, bool modifyStamp) {
    if (!pts) {
        // 没有播放时间戳,使其赋值为解码时间戳
        pts = dts;
    }

    if (_playback) {
        // 这是点播
        dts_out = dts;
        pts_out = pts;
        _relative_stamp = dts_out;
        _last_dts_in = dts;
        return;
    }

    // 记录pts和dts的差值，等下用于还原pts
    int64_t pts_dts_diff = pts - dts;

    if (_last_dts_in != dts) {
        // dts changed
        if (modifyStamp) {
            // use ticker timestamps instead of input dts
            _relative_stamp = _ticker.elapsedTime();
        } else {
            _relative_stamp += deltaStamp(dts, _enable_rollback);
        }
        _last_dts_in = dts;
    }
    dts_out = _relative_stamp;
    GET_CONFIG(uint32_t, max_cts, kMaxCts);
    // update pts_out according to dts_out and pts_dts_diff
    if (ABS(pts_dts_diff) > max_cts) {
        // 如果差值太大，则认为由于回环导致时间戳错乱了
        pts_dts_diff = 0;
    }
    pts_out = dts_out + pts_dts_diff;
}

///////////////////////////////////////////////////////////
// DtsGenerator实现
bool DtsGenerator::getDts(uint64_t pts, uint64_t &dts) {
    bool ret = false;
    if (pts == _last_pts) {
        // pts未变，说明dts也不会变，返回上次dts
        if (_last_dts) {
            dts = _last_dts;
            ret = true;
        }
    } else {
        // pts变了，尝试计算dts
        ret = getDts_l(pts, dts);
        if (ret) {
            // 获取到了dts，保存本次结果
            _last_dts = dts;
        }
    }

    if (!ret) {
        // pts排序列队长度还不知道，也就是不知道有没有B帧
        // 那么先强制dts == pts，这样可能导致有B帧的情况下，起始画面有几帧回退
        dts = pts;
    }

    // 记录上次pts
    _last_pts = pts;
    return ret;
}

// 该算法核心思想是对pts进行排序，排序好的pts就是dts。
// 排序有一定的滞后性，那么需要加上排序导致的时间戳偏移量
bool DtsGenerator::getDts_l(uint64_t pts, uint64_t &dts) {
    if (_sorter_max_size == 1) {
        // 没有B帧，dts就等于pts  [AUTO-TRANSLATED:9cfae4ea]
        // There is no B frame, dts is equal to pts
        dts = pts;
        return true;
    }

    if (!_sorter_max_size) {
        // 尚未计算出pts排序列队长度(也就是P帧间B帧个数)
        if (pts > _last_max_pts) {
            // pts时间戳增加了，那么说明这帧画面不是B帧(说明是P帧或关键帧)
            if (_frames_since_last_max_pts && _count_sorter_max_size++ > 0) {
                // 已经出现多次非B帧的情况，那么我们就能知道P帧间B帧的个数
                _sorter_max_size = _frames_since_last_max_pts;
                // 我们记录P帧间时间间隔(也就是多个B帧时间戳增量累计)
                _dts_pts_offset = (pts - _last_max_pts);
                // 除以2，防止dts大于pts
                _dts_pts_offset /= 2;
            }
            // 遇到P帧或关键帧，连续B帧计数清零
            _frames_since_last_max_pts = 0;
            // 记录上次非B帧的pts时间戳(同时也是dts)，用于统计连续B帧时间戳增量
            _last_max_pts = pts;
        }
        // 如果pts时间戳小于上一个P帧，那么断定这个是B帧,我们记录B帧连续个数
        ++_frames_since_last_max_pts;
    }

    // pts放入排序缓存列队，缓存列队最大等于连续B帧个数
    _pts_sorter.emplace(pts);

    if (_sorter_max_size > 1 && _pts_sorter.size() > _sorter_max_size) {
        // 如果启用了pts排序(意味着存在B帧)，并且pts排序缓存列队长度大于连续B帧个数，
        // 意味着后续的pts都会比最早的pts大，那么说明可以取出最早的pts了，这个pts将当做该帧的dts基准
        auto it = _pts_sorter.begin();

        // 由于该pts是前面偏移了个_sorter_max_size帧的pts(也就是那帧画面的dts),
        // 那么我们加上时间戳偏移量，基本等于该帧的dts
        dts = *it + _dts_pts_offset;
        if (dts > pts) {
            // dts不能大于pts(基本不可能到达这个逻辑)
            dts = pts;
        }

        // pts排序缓存出列
        _pts_sorter.erase(it);
        return true;
    }

    // 排序缓存尚未满
    return false;
}

///////////////////////////////////////////////////////////
// NtpStamp实现
int NtpStamp::setNtpStamp(uint32_t rtp_stamp, uint64_t ntp_stamp_ms) {
    int delta = 0;
    if (!ntp_stamp_ms || !rtp_stamp) {
        // 有些rtsp服务器发的rtp时间戳和ntp时间戳一直为0
        WarnL << "Invalid sender report rtcp, ntp_stamp_ms = " << ntp_stamp_ms << ", rtp_stamp = " << rtp_stamp;
        return delta;
    }
    if (_last_sample_rate) {
        delta = (int64_t)ntp_stamp_ms - getNtpStamp(rtp_stamp, _last_sample_rate);
    }
    update(rtp_stamp, ntp_stamp_ms * 1000);
    return delta;
}

void NtpStamp::update(uint32_t rtp_stamp, uint64_t ntp_stamp_us) {
    _last_rtp_stamp = rtp_stamp;
    _last_ntp_stamp_us = ntp_stamp_us;
}

uint64_t NtpStamp::getNtpStamp(uint32_t rtp_stamp, uint32_t sample_rate) {
    if (_last_sample_rate != sample_rate) {
        _last_sample_rate = sample_rate;
    }
    if (rtp_stamp == _last_rtp_stamp) {
        return _last_ntp_stamp_us / 1000;
    }
    return getNtpStampUS(rtp_stamp, sample_rate) / 1000;
}

uint64_t NtpStamp::getNtpStampUS(uint32_t rtp_stamp, uint32_t sample_rate) {
    if (!_last_ntp_stamp_us) {
        // 还未收到SR包，则将ntp设为接收时间戳
        update(rtp_stamp, getCurrentMicrosecond(true));
    }
    GET_CONFIG(uint32_t, max_ntp_delta, kMaxNtpDelta);
    GET_CONFIG(uint32_t, ntp_loop_delta, kNtpLoopDelta);
    // The rtp timestamp is increasing
    if (rtp_stamp >= _last_rtp_stamp) {
        auto diff_us = static_cast<int64_t>((rtp_stamp - _last_rtp_stamp) / (sample_rate / 1000000.0f));
        if (diff_us < max_ntp_delta * 1000) {
            // The timestamp is increasing normally
            update(rtp_stamp, _last_ntp_stamp_us + diff_us);
            return _last_ntp_stamp_us;
        }

        // The timestamp jumps significantly 大幅跳跃
        uint64_t loop_delta_hz = ntp_loop_delta * sample_rate / 1000;
        if (_last_rtp_stamp < loop_delta_hz && rtp_stamp > UINT32_MAX - loop_delta_hz) {
            // It should be rtp timestamp overflow + out of order
            uint64_t max_rtp_us = uint64_t(UINT32_MAX) * 1000000 / sample_rate;
            return _last_ntp_stamp_us + diff_us - max_rtp_us;
        }

        // The timestamp jumps significantly for unknown reasons, directly return the last value
        WarnL << "rtp stamp abnormal increased:" << _last_rtp_stamp << " -> " << rtp_stamp;
        update(rtp_stamp, _last_ntp_stamp_us);
        return _last_ntp_stamp_us;
    }

    // The rtp timestamp is decreasing
    auto diff_us = static_cast<int64_t>((_last_rtp_stamp - rtp_stamp) / (sample_rate / 1000000.0f));
    if (diff_us < max_ntp_delta * 1000) {
        // The timestamp is decreasing normally, indicating that rtp is out of order
        return _last_ntp_stamp_us - diff_us;
    }

    // The timestamp decreases significantly (大幅回退)
    uint64_t loop_delta_hz = ntp_loop_delta * sample_rate / 1000;
    if (rtp_stamp < loop_delta_hz && _last_rtp_stamp > UINT32_MAX - loop_delta_hz) {
        // timestamp overflow
        uint64_t max_rtp_us = uint64_t(UINT32_MAX) * 1000000 / sample_rate;
        update(rtp_stamp, _last_ntp_stamp_us + (max_rtp_us - diff_us));
        return _last_ntp_stamp_us;
    }

    // Timestamp rollback for unknown reasons, return the last value directly
    WarnL << "rtp stamp abnormal reduced:" << _last_rtp_stamp << " -> " << rtp_stamp;
    update(rtp_stamp, _last_ntp_stamp_us);
    return _last_ntp_stamp_us;
}

} // namespace mediakit
