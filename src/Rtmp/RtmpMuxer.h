/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_RTMPMUXER_H
#define ZLMEDIAKIT_RTMPMUXER_H

#include "Rtmp/Rtmp.h"
#include "Extension/Frame.h"
#include "Common/MediaSink.h"
// RtmpRing
#include "RtmpCodec.h"

namespace mediakit{
/*
Rtmp混合器.
- 拦截MediaSink的addTrack回调，生成metadata
- 处理MediaSink的inputFrame，编码后传给 RtmpRing
*/
class RtmpMuxer : public MediaSinkInterface {
public:
    typedef std::shared_ptr<RtmpMuxer> Ptr;

    /**
     * 构造函数
     */
    RtmpMuxer(const TitleMeta::Ptr &title);
    ~RtmpMuxer() override = default;

    /**
     * 获取完整的MetaData头部
     */
    const AMFValue &getMetadata() const;

    /**
     * 获取rtmp环形缓存，用于输出数据
     */
    RtmpRing::RingType::Ptr getRtmpRing() const;

    /**
     * 添加ready状态的track
     */
    bool addTrack(const Track::Ptr & track) override;

    /**
     * 写入帧数据，最终数据从_rtmp_ring出
     * @param frame 帧
     */
    bool inputFrame(const Frame::Ptr &frame) override;

    /**
     * 刷新输出所有frame缓存
     */
    void flush() override;

    /**
     * 重置所有track
     */
    void resetTracks() override ;

    /**
     * 生成config包
     */
     void makeConfigPacket();
     /**
      * 获取所有的config帧
      */
     template<typename FUNC>
     void getConfigFrame(const FUNC &f) {
         for (auto &pr : _encoder) {
             if (!pr)
                 continue;
             if (auto pkt = pr->makeConfigPacket())
                f(pkt);
         }
     }
     bool haveAudio() const { return nullptr != _encoder[TrackAudio]; }
     bool haveVideo() const { return nullptr != _encoder[TrackVideo]; }
private:
    RtmpRing::RingType::Ptr _rtmp_ring;
    AMFValue _metadata;
    RtmpCodec::Ptr _encoder[TrackMax];
};


} /* namespace mediakit */

#endif //ZLMEDIAKIT_RTMPMUXER_H
