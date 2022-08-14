/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#if defined(ENABLE_RTPPROXY)
#include "Util/uv_errno.h"
#include "RtpServer.h"
#include "RtpSelector.h"
#include "Rtcp/RtcpContext.h"
#include "Common/config.h"

using namespace std;
using namespace toolkit;

namespace mediakit{

RtpServer::~RtpServer() {
    if (_on_cleanup) {
        _on_cleanup();
    }
}

class RtcpHelper: public std::enable_shared_from_this<RtcpHelper> {
public:
    using Ptr = std::shared_ptr<RtcpHelper>;

    RtcpHelper(Session::Ptr rtcp_sock, std::string stream_id) {
        _rtcp_sock = std::move(rtcp_sock);
        _stream_id = std::move(stream_id);
    }

    ~RtcpHelper() {
        if (_process) {
            // 删除rtp处理器
            RtpSelector::Instance().delProcess(_stream_id, _process.get());
        }
    }

    void setRtpServerInfo(uint16_t local_port,RtpServer::TcpMode mode,bool re_use_port,uint32_t ssrc){
        _local_port = local_port;
        _tcp_mode = mode;
        _re_use_port = re_use_port;
        _ssrc = ssrc;
    }

    void setOnDetach(function<void()> cb) {
        if (_process) {
            _process->setOnDetach(std::move(cb));
        } else {
            _on_detach = std::move(cb);
        }
    }

    void onRecvRtp(const Session::Ptr &sock, hv::Buffer* buf, struct sockaddr *addr) {
        if (!_process) {
            _process = RtpSelector::Instance().getProcess(_stream_id, true);
            _process->setOnDetach(std::move(_on_detach));
            cancelDelayTask();
        }
        _process->inputRtp(true, sock, (const char*)buf->data(), buf->size(), addr);

        // 统计rtp接受情况，用于发送rr包
        auto header = (RtpHeader *)buf->data();
        sendRtcp(ntohl(header->ssrc), addr);
    }

    void startRtcp() {
        hio_t* io = _rtcp_sock->io();
        std::weak_ptr<RtcpHelper> weak_self = shared_from_this();
        _rtcp_sock->onread = [weak_self, io](hv::Buffer* buf) {
            // 用于接受rtcp打洞包
            auto strong_self = weak_self.lock();
            if (!strong_self || !strong_self->_process) {
                return;
            }
            if (!strong_self->_rtcp_addr) {
                // 只设置一次rtcp对端端口
                strong_self->_rtcp_addr = hio_peeraddr(io);
            }
            auto rtcps = RtcpHeader::loadFromBytes((char*)buf->data(), buf->size());
            for (auto &rtcp : rtcps) {
                strong_self->_process->onRtcp(rtcp);
            }
        };

        GET_CONFIG(uint64_t, timeoutSec, RtpProxy::kTimeoutSec);
        _delay_task = _rtcp_sock->getPoller()->doDelayTask(timeoutSec * 1000, [weak_self]() {
            if (auto strong_self = weak_self.lock()) {
                auto process = RtpSelector::Instance().getProcess(strong_self->_stream_id, false);
                if (!process && strong_self->_on_detach) {
                    strong_self->_on_detach();
                }
                if(process && strong_self->_on_detach){// tcp 链接防止断开不删除rtpServer
                    process->setOnDetach(std::move(strong_self->_on_detach));
                }
                if (!process) { // process 未创建，触发rtp server 超时事件
                    NoticeCenter::Instance().emitEvent(Broadcast::KBroadcastRtpServerTimeout,strong_self->_local_port,strong_self->_stream_id,(int)strong_self->_tcp_mode,strong_self->_re_use_port,strong_self->_ssrc);
                }
            }
            return 0;
        });
    }

    void cancelDelayTask() {
        if (_delay_task) {
            _rtcp_sock->getPoller()->killTimer(_delay_task);
            _delay_task = 0;
        }
    }

private:
    void sendRtcp(uint32_t rtp_ssrc, struct sockaddr *addr) {
        // 每5秒发送一次rtcp
        if (_ticker.elapsedTime() < 5000 || !_process) {
            return;
        }
        _ticker.resetTime();

        auto rtcp_addr = _rtcp_addr;
        if (!rtcp_addr) {
            // guess rtcpPort = rtpPort + 1
            sockaddr_set_port((sockaddr_u*)addr, sockaddr_port((sockaddr_u*)addr)+1);
            // 未收到对方rtcp打洞包时，采用缺省/猜测的rtcp端口
            rtcp_addr = addr;
        }

        auto buf = _process->createRtcpRR(rtp_ssrc + 1, rtp_ssrc);
        int addr_len = sockaddr_len((sockaddr_u*)rtcp_addr);
        hio_set_peeraddr(_rtcp_sock->io(), rtcp_addr, addr_len);
        _rtcp_sock->write(buf->data(), buf->size());
    }

private:
    bool _re_use_port = false;
    uint16_t _local_port = 0;
    uint32_t _ssrc = 0;
    RtpServer::TcpMode _tcp_mode = RtpServer::NONE;

    Ticker _ticker;
    Session::Ptr _rtcp_sock;
    RtpProcess::Ptr _process;
    std::string _stream_id;
    function<void()> _on_detach;
    sockaddr* _rtcp_addr;
    hv::TimerID _delay_task;
};

void RtpServer::start(uint16_t local_port, const string &stream_id, TcpMode tcp_mode, const char *local_ip, bool re_use_port, uint32_t ssrc) {
    //创建udp服务器
    Session::Ptr rtp_socket;
    Session::Ptr rtcp_socket;
    if (local_port == 0) {
        //随机端口，rtp端口采用偶数
        toolkit::SessionPtr pair[2];
        makeSockPair(pair, local_ip, re_use_port);
        rtp_socket = pair[0];
        rtcp_socket = pair[1];
    }
    else {
        hloop_t* loop = hv::tlsEventLoop()->loop();
        hio_t* io1 = hloop_create_udp_server(loop, local_ip, local_port);
        hio_t* io2 = hloop_create_udp_server(loop, local_ip, local_port + 1);
        if (!io1) {
            //用户指定端口
            throw std::runtime_error(StrPrinter << "创建rtp端口 " << local_ip << ":" << local_port << " 失败:"  << get_uv_errmsg(true));
        }
        if (!io2) {
            // rtcp端口
            throw std::runtime_error(StrPrinter << "创建rtcp端口 " << local_ip << ":" << local_port + 1 << " 失败:" << get_uv_errmsg(true));
            hio_close(io1);
        }
        rtp_socket = std::make_shared<toolkit::Session>(io1);
        rtcp_socket = std::make_shared<toolkit::Session>(io2);
    }

    //设置udp socket读缓存
    so_rcvbuf(rtp_socket->fd(), 4 * 1024 * 1024);

    _tcp_mode = tcp_mode;
    if (tcp_mode == PASSIVE || tcp_mode == ACTIVE) {
        //创建tcp服务器
        _tcp_server = std::make_shared<TcpServer>(rtp_socket->getPoller());
#if 0
        (*tcp_server)[RtpSession::kStreamID] = stream_id;
        (*tcp_server)[RtpSession::kSSRC] = ssrc;
#endif
        if (tcp_mode == PASSIVE) {
            _tcp_server->createsocket(rtp_socket->get_local_port(), local_ip);
            _tcp_server->start();
        } else if (stream_id.empty()) {
            // tcp主动模式时只能一个端口一个流，必须指定流id; 创建TcpServer对象也仅用于传参
            throw std::runtime_error("tcp主动模式时必需指定流id");
        }
    }

    //创建udp服务器
    RtcpHelper::Ptr helper;
    if (!stream_id.empty()) {
        //指定了流id，那么一个端口一个流(不管是否包含多个ssrc的多个流，绑定rtp源后，会筛选掉ip端口不匹配的流)
        helper = std::make_shared<RtcpHelper>(std::move(rtcp_socket), stream_id);
        helper->startRtcp();
        helper->setRtpServerInfo(local_port,tcp_mode,re_use_port,ssrc);
        rtp_socket->onread = [rtp_socket, helper, ssrc](hv::Buffer* buf) {
            RtpHeader *header = (RtpHeader *)buf->data();
            auto rtp_ssrc = ntohl(header->ssrc);
            if (ssrc && rtp_ssrc != ssrc) {
                WarnL << "ssrc不匹配,rtp已丢弃:" << rtp_ssrc << " != " << ssrc;
            } else {
                struct sockaddr *addr = hio_peeraddr(rtp_socket->io());
                helper->onRecvRtp(rtp_socket, buf, addr);
            }
        };
    } else {
#if 1
        //单端口多线程接收多个流，根据客户端ip+port区分RtpSession，根据ssrc区分流(RtpSelector)
        _udp_server = std::make_shared<UdpServer>(rtp_socket->getPoller());
        // 此时的rtp_socket只是来判断端口是否可用而已，数据由udp_server接管，后者可以做到一个peer一个线程
        _udp_server->createsocket(rtp_socket->get_local_port(), local_ip);
        _udp_server->start();
        // 这边的rtp_socket只起到占用端口作用
        rtp_socket = nullptr;
#else
        //单端口单线程接收多个流
        rtp_socket->setOnRead([rtp_socket](const Buffer::Ptr &buf, struct sockaddr *addr, int) {
            RtpSelector::Instance().inputRtp(rtp_socket, buf->data(), buf->size(), addr);
        });
#endif
    }

    _on_cleanup = [rtp_socket, stream_id]() {
        if (rtp_socket) {
            //去除循环引用
            rtp_socket->onread = nullptr;
        }
    };

    _rtp_socket = rtp_socket;
    _rtcp_helper = helper;
}

void RtpServer::setOnDetach(function<void()> cb) {
    if (_rtcp_helper) {
        _rtcp_helper->setOnDetach(std::move(cb));
    }
}

uint16_t RtpServer::getPort() {
    return _udp_server ? sockaddr_port((sockaddr_u*)hio_localaddr(_udp_server->listenio)) : _rtp_socket->get_local_port();
}

void RtpServer::connectToServer(const std::string &url, uint16_t port, const function<void(const SockException &ex)> &cb) {
    if (_tcp_mode != ACTIVE || !_rtp_socket) {
        cb(SockException(Err_other, "仅支持tcp主动模式"));
        return;
    }
    weak_ptr<RtpServer> weak_self = shared_from_this();
	auto loop = hv::EventLoopThreadPool::Instance()->loop();
#if 0
    _rtp_socket->connect(url, port, [url, port, cb, weak_self](const SockException &err) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            cb(SockException(Err_other, "服务对象已释放"));
            return;
        }
        if (err) {
            WarnL << "连接到服务器 " << url << ":" << port << " 失败 " << err.what();
        } else {
			InfoL << "连接到服务器 " << url << ":" << port << " 成功";
            strong_self->onConnect();
        }
        cb(err);
    },
    5.0F, "::", _rtp_socket->get_local_port());
#endif
}

void RtpServer::onConnect(hio_t* io) {
    auto rtp_session = std::make_shared<RtpSession>(io);
    //rtp_session->attachServer(*_tcp_server);
    _rtp_socket->onread = [rtp_session](hv::Buffer* buf) {
		auto buff = BufferRaw::create();
		buff->assign((const char*)buf->data(), buf->size());
        rtp_session->onRecv(buff);
    };
    weak_ptr<RtpServer> weak_self = shared_from_this();
	_rtp_socket->onclose = [weak_self]() {
        if (auto strong_self = weak_self.lock()) {
            strong_self->_rtp_socket->onread = nullptr;
        }
    };
}

}//namespace mediakit
#endif//defined(ENABLE_RTPPROXY)
