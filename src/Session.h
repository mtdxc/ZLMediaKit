#ifndef SRC_SOCKET_TOOLKIT_H_
#define SRC_SOCKET_TOOLKIT_H_

#include "Channel.h"
#include "toolkit.h"

namespace toolkit {

class Session : public hv::SocketChannel, public std::enable_shared_from_this<Session> {
public:
    typedef std::shared_ptr<Session> Ptr;
    Session(hio_t* io);

    void flush(){}
    void setSendFlushFlag(bool v){}
    virtual void onError(const SockException &err) {}
    virtual void onManager() {}
    void shutdown(const SockException& e, bool safe = false);
};

}
#endif