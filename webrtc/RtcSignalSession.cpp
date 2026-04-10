#include "Network/TcpServer.h"
#include "Network/Session.h"
#include "json/json.h"
#include <memory>
using namespace toolkit;

#define CMD_KEY "cmd"
#define TO_KEY "to"
#define ID_KEY "id"
#define DATA_KEY "data"
#define ERROR_KEY "error"
#define VAL_KEY "val"
#define SRC_KEY "src"
struct MsgHdr {
    uint8_t cls:2; // 1: request, 2: success resp, 3: error resp, 0: indication
    uint8_t method:6; // 0 login, 1 logout, 2 sub, 3 unsub, 4 get, 5 set, 6 pub, 7 bye
    uint32_t tid;
};
// webrtc 信令, 基于websocket实现
class RtcSignalingSession : public Session {
public:
    using Ptr = std::shared_ptr<RtcSignalingSession>;
    using WeakPtr = std::weak_ptr<RtcSignalingSession>;

    RtcSignalingSession(const toolkit::Socket::Ptr &sock)
        : Session(sock) {}
    ~RtcSignalingSession() override {}

    void onError(const SockException &err) override;
    void onRecv(const toolkit::Buffer::Ptr &buffer) override;
    void onManager() override {}

    void sendPacket(const Json::Value &body) {
        send(body.toStyledString());
    }
private:
    std::string _id;
    std::list<RtcSignalingSession::WeakPtr> _listener;
};

// 注册上来的peer列表
static std::atomic<uint32_t> s_room_idx_generate { 1 };
// 用户列表
static std::map<std::string, RtcSignalingSession::Ptr> s_peers;
static std::mutex s_rooms_mutex;
// 环境变量
static std::map<std::string, std::string> s_vars;
// 订阅列表
static std::map<std::string, std::list<RtcSignalingSession::WeakPtr>> s_topics;

RtcSignalingSession::Ptr getPeer(const std::string &id) {
    std::unique_lock<std::mutex> lock(s_rooms_mutex);
    auto it = s_peers.find(id);
    if (it != s_peers.end()) {
        return it->second;
    }
    return nullptr;
}

bool getVar(const std::string &key, std::string& val) {
    std::unique_lock<std::mutex> lock(s_rooms_mutex);
    auto it = s_vars.find(key);
    if (it != s_vars.end()) {
        val = it->second;
        return true;
    }
    return false;
}

bool setVar(const std::string &key, const std::string& val) {
    std::unique_lock<std::mutex> lock(s_rooms_mutex);
    s_vars[key] = val;
    return true;
}

int subsTopic(const std::string& topic, RtcSignalingSession::Ptr session) {
    std::unique_lock<std::mutex> lock(s_rooms_mutex);
    auto& list = s_topics[topic];
    list.emplace_back(session);
    return list.size();
}

void unsubsTopic(const std::string& topic, RtcSignalingSession::Ptr session) {
    std::unique_lock<std::mutex> lock(s_rooms_mutex);
    auto& list = s_topics[topic];
    list.remove_if([session](const RtcSignalingSession::WeakPtr &p) {
        return p.lock() == session;
    });
}

int forEachTopic(const std::string &topic, const std::function<void(const RtcSignalingSession::Ptr &p)> &cb) {
    int ret = 0;
    std::unique_lock<std::mutex> lock(s_rooms_mutex);
    auto it = s_topics.find(topic);
    if (it != s_topics.end()) {
        auto &list = it->second;
        for (auto iter = list.begin(); iter != list.end();) {
            auto ptr = iter->lock();
            if (ptr) {
                cb(ptr);
                ++iter;
                ret++;
            } else {
                iter = list.erase(iter);
            }
        }
        if (list.empty()) {
            s_topics.erase(it);
        }
    }
    return ret;
}


void RtcSignalingSession::onRecv(const Buffer::Ptr &buffer) {
    DebugL << "recv msg:\r\n" << buffer->data();

    Json::Value args;
    Json::Reader reader;
    reader.parse(buffer->data(), args);
    auto cmd = args[CMD_KEY].asString();
    auto to = args[TO_KEY].asString();
    if (cmd == "register") { // 登录获取id
        // 如果客户端没有提供 room_id，服务端自动分配一个
        std::string room_id = to;
        if (room_id.empty()) {
            auto idx = s_room_idx_generate.fetch_add(1);
            room_id = std::to_string(idx) + "_" + makeRandStr(16);
            DebugL << "auto generated room_id: " << room_id;
            args[TO_KEY] = room_id;
        } else {
            std::unique_lock<std::mutex> lock(s_rooms_mutex);
            if (s_peers.count(room_id)) {
                // 已经注册了
                args[DATA_KEY] = 0;
                args[ERROR_KEY] = "room id conflict";
                sendPacket(args);
                return ;
            }
        }

        args[DATA_KEY] = 1;
        _id = room_id;
        std::unique_lock<std::mutex> lock(s_rooms_mutex);
        s_peers[_id] = std::static_pointer_cast<RtcSignalingSession>(shared_from_this());
        sendPacket(args);
    } else if (cmd == "unregister") { // 取消登录
       shutdown(SockException(Err_shutdown, "unregister"));
    }
    else if (cmd == "subs") {
        auto pThis = std::static_pointer_cast<RtcSignalingSession>(shared_from_this());
        if(auto s = getPeer(to)) {
            s->_listener.emplace_back(pThis);
        } else {
            subsTopic(to, pThis);
        }
    }
    else if (cmd == "unsubs") {
        auto pThis = std::static_pointer_cast<RtcSignalingSession>(shared_from_this());
        if(auto s = getPeer(to)) {
            s->_listener.remove_if([pThis](const RtcSignalingSession::WeakPtr &p) {
                return p.lock() == pThis;
            });
        } else {
             unsubsTopic(to, pThis);
        }
    }
    else if (cmd == "msg") {
        args[SRC_KEY] = _id;
        auto pThis = std::static_pointer_cast<RtcSignalingSession>(shared_from_this());
        std::string msg = args.toStyledString();
        if (auto s = getPeer(to)) {
            s->send(msg);
            args[DATA_KEY] = 1;
        }
        else{
            // 查找和转发消息
            args[DATA_KEY] = forEachTopic(to, [msg, pThis](const RtcSignalingSession::Ptr &p) {
                if (p!=pThis) {
                    p->send(msg);
                }
            });
        }
        sendPacket(args);
    }
    else if(cmd=="get") {
        std::string val;
        if (!getVar(to, val)) {
            val = "null";
        }
        args[VAL_KEY] = val;
        sendPacket(args);
    } else if(cmd=="set") {
        args[VAL_KEY] = setVar(to, args[VAL_KEY].asString());
        sendPacket(args);
    } else {
        WarnL << "unsupported cmd: " << cmd;
    }
}

void RtcSignalingSession::onError(const SockException &err) {
    WarnL << "room_id: " << _id;
    // notifyByeIndication();
    s_peers.erase(_id);
}

#include "Http/WebSocketClient.h"
class RtcSignalingPeer : public mediakit::WebSocketClient<TcpClient> {
    std::string _peer_id;
    using Ptr = std::shared_ptr<RtcSignalingPeer>;
    RtcSignalingPeer(const std::string &host, uint16_t port, bool ssl, const toolkit::EventPoller::Ptr &poller = nullptr)
        : mediakit::WebSocketClient<TcpClient>(poller) {
    }

    virtual ~RtcSignalingPeer();

    void connect(const std::string &id) {

    }
    void sendPacket(const Json::Value& body){
        auto msg = body.toStyledString();
        TraceL << "send msg: " << msg;
        SockSender::send(msg);
    }
    void subs(const std::string& topic) {
        Json::Value body;
        body[CMD_KEY] = "subs";
        body[TO_KEY] = topic;
        sendPacket(body);
    }
    void unsubs(const std::string& topic) {
        Json::Value body;
        body[CMD_KEY] = "unsubs";
        body[TO_KEY] = topic;
        sendPacket(body);
    }
    void sendMsg(const std::string& id, const Json::Value& data) {
        Json::Value body;
        body[CMD_KEY] = "msg";
        body[TO_KEY] = id;
        body[DATA_KEY] = data;
        sendPacket(body);
    }
};
