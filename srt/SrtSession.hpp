#ifndef ZLMEDIAKIT_SRT_SESSION_H
#define ZLMEDIAKIT_SRT_SESSION_H

#include "Session.h"
#include "Util/TimeTicker.h"
#include "Buffer.hpp"
namespace SRT {

class SrtTransport;

class SrtSession : public toolkit::Session {
public:
    typedef std::shared_ptr<SrtSession> Ptr;

    SrtSession(hio_t* io);
    ~SrtSession() override;

    void onRecv(uint8_t *data, size_t size);
    void onError(const toolkit::SockException &err);
    void onManager();
    static toolkit::EventPollerPtr queryPoller(uint8_t *data, size_t size);
private:
    bool _find_transport = true;
    toolkit::Ticker _ticker;
    struct sockaddr_storage* _peer_addr;
    std::shared_ptr<SrtTransport> _transport;
};

} // namespace SRT
#endif // ZLMEDIAKIT_SRT_SESSION_H