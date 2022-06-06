#ifndef ZLMEDIAKIT_SRT_PACKET_SEND_QUEUE_H
#define ZLMEDIAKIT_SRT_PACKET_SEND_QUEUE_H

#include "Packet.hpp"
#include <list>

namespace SRT {

class PacketSendQueue {
public:
    using Ptr = std::shared_ptr<PacketSendQueue>;
    using LostPair = std::pair<uint32_t, uint32_t>;

    PacketSendQueue(uint32_t max_size, uint32_t latency,uint32_t flag = 0xbf);
    ~PacketSendQueue() = default;

    bool inputPacket(DataPacket::Ptr pkt);
    std::list<DataPacket::Ptr> findPacketBySeq(uint32_t start, uint32_t end);
    // 丢弃seq_num之前的所有包
    bool drop(uint32_t seq_num);
private:
    uint32_t timeLatency();
    bool TLPKTDrop();
private:
    uint32_t _srt_flag;
    // 容量限制
    uint32_t _pkt_cap;
    // 延时限制
    uint32_t _pkt_latency;
    // 用Map会不会高效点?
    std::list<DataPacket::Ptr> _pkt_cache;
};

} // namespace SRT

#endif // ZLMEDIAKIT_SRT_PACKET_SEND_QUEUE_H