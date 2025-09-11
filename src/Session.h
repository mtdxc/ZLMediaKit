#ifndef SRC_SOCKET_TOOLKIT_H_
#define SRC_SOCKET_TOOLKIT_H_

#include "Channel.h"
#include "toolkit.h"
#include <string>
namespace toolkit {

class Session : public hv::SocketChannel, public std::enable_shared_from_this<Session> {
    std::string _id;
public:
    typedef std::shared_ptr<Session> Ptr;
    Session(hio_t* io);

    void flush(){}
    void setSendFlushFlag(bool v){}
    virtual void onError(const SockException &err) {}
    virtual void onManager() {}
    void shutdown(const SockException& e, bool safe = false);
    std::string getIdentifier() {
        if (_id.empty()) {
            _id = std::to_string(id()) + '-' + std::to_string(fd());
        }
        return _id;
    }
};

}
#endif