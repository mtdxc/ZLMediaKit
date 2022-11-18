#ifndef SRC_TCPCLIENT_TOOLKIT_H_
#define SRC_TCPCLIENT_TOOLKIT_H_

#include "Buffer.hpp"
#include "TcpClient.h"
#include "toolkit.h"

namespace toolkit {

class TcpClient : public hv::TcpClient, 
    public std::enable_shared_from_this<TcpClient> {
public:
    TcpClient(EventPollerPtr loop = NULL);
    /**
     * 主动断开连接
     * @param ex 触发onErr事件时的参数
     */
    void shutdown(const SockException &ex = SockException(Err_shutdown, "self shutdown"));

    /**
     * 开始连接tcp服务器
     * @param url 服务器ip或域名
     * @param port 服务器端口
     * @param timeout_sec 超时时间,单位秒
     * @param local_port 本地端口
     */
    void startConnect(const std::string &url, uint16_t port, float timeout_sec = 5, uint16_t local_port = 0){
        setConnectTimeout(timeout_sec*1000);
        bind(local_port, local_host.c_str());
        createsocket(port, url.c_str());
    }
    void setNetAdapter(const std::string &local_ip) {
        this->local_host = local_ip;
    }
protected:
    std::string local_host;
    /**
     * 连接服务器结果回调
     * @param ex 成功与否
     */
    virtual void onConnect(const SockException &ex) = 0;

    /**
     * 收到数据回调
     * @param buf 接收到的数据(该buffer会重复使用)
     */
    virtual void onRecv(const Buffer::Ptr &buf) = 0;

    /**
     * 数据全部发送完毕后回调
     */
    virtual void onFlush() {}

    /**
     * 被动断开连接回调
     * @param ex 断开原因
     */
    virtual void onErr(const SockException &ex) = 0;

    /**
     * tcp连接成功后每2秒触发一次该事件
     */
    virtual void onManager() {}    
};
}
#endif