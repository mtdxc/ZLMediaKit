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
#include "RtpSender.h"
#include "Rtsp/RtspSession.h"
#include "EventLoopThreadPool.h"
#include "Util/TimeTicker.h"
#include "RtpCache.h"
#include "hsocket.h"
using namespace std;
using namespace toolkit;

namespace mediakit{

RtpSender::RtpSender(EventPoller::Ptr poller) {
    _poller = poller ? std::move(poller) : hv::EventLoopThreadPool::Instance()->loop();
    //_socket_rtp = Socket::createSocket(_poller, false);
}

RtpSender::~RtpSender() {
    flush();
}

void RtpSender::startSend(const MediaSourceEvent::SendRtpArgs &args, const function<void(uint16_t local_port, const SockException &ex)> &cb){
    _args = args;
    if (!_interface) {
        //重连时不重新创建对象
        auto lam = [this](std::shared_ptr<std::list<Buffer::Ptr>> list) { onFlushRtpList(std::move(list)); };
        if (args.use_ps) {
            _interface = std::make_shared<RtpCachePS>(lam, atoi(args.ssrc.data()), args.pt);
        } else {
            _interface = std::make_shared<RtpCacheRaw>(lam, atoi(args.ssrc.data()), args.pt, args.only_audio);
        }
    }

    weak_ptr<RtpSender> weak_self = shared_from_this();
    if (args.passive) {
        // tcp被动发流模式
        _args.is_udp = false;
        // 默认等待链接
        bool is_wait = true;
        try {
            toolkit::SessionPtr tcp_listener = nullptr;
            if (args.src_port) {
                hio_t* io = hio_create_socket(_poller->loop(), "::", args.src_port);
                //指定端口
                if (!io) {
                    throw std::invalid_argument(StrPrinter << "open tcp passive server failed on port:" << args.src_port);
                }
                tcp_listener = std::make_shared<toolkit::Session>(io);
                is_wait = true;
            } else {
                toolkit::SessionPtr pr[2];
                //从端口池获取随机端口
                makeSockPair(pr, "::", false, false);
                tcp_listener = pr[0];
                // 随机端口不等待，保证调用者可以知道端口
                is_wait = false;
            }
            // tcp服务器默认开启5秒
            auto delay_task = _poller->doDelayTask(_args.tcp_passive_close_delay_ms, [tcp_listener, cb,is_wait]() mutable {
                if (is_wait) {
                    cb(0, SockException(Err_timeout, "wait tcp connection timeout"));
                }
                tcp_listener = nullptr;
                return 0;
            });
            typedef std::function<void(hio_t*)> AcceptCB;
            AcceptCB* acceptCb = new AcceptCB([weak_self, is_wait, cb, delay_task](hio_t* io) {
                auto strong_self = weak_self.lock();
                if (!strong_self) {
                    return;
                }
                //立即关闭tcp服务器
                hv::killTimer(delay_task);
                strong_self->_socket_rtp = std::make_shared<toolkit::Session>(io);
                strong_self->onConnect();
                if (is_wait) {
                    cb(strong_self->_socket_rtp->get_local_port(), SockException());
                }
                InfoL << "accept connection from:" << strong_self->_socket_rtp->peeraddr();
            });

            hio_t* io = tcp_listener->io();
            hevent_set_userdata(io, acceptCb);
            hio_setcb_accept(tcp_listener->io(), [](hio_t* io) {
                if (AcceptCB* pCB = (AcceptCB*)hevent_userdata(io)) {
                    (*pCB)(io);
                    delete pCB;
                }
            });
            hio_accept(tcp_listener->io());
            InfoL << "start tcp passive server on:" << tcp_listener->get_local_port();
            if (!is_wait) {
                // 随机端口马上返回端口，保证调用者知道端口
                cb(tcp_listener->get_local_port(), SockException());
            }
        } catch (std::exception &ex) {
            cb(0, SockException(Err_other, ex.what()));
            return;
        }
        return;
    }
    if (args.is_udp) {
        auto poller = _poller;
        hv::EventLoopThreadPool::Instance()->loop()->async([cb, args, weak_self, poller]() {
            sockaddr_u addr;
            // 切换线程目的是为了dns解析放在后台线程执行
            if (!ResolveAddr(args.dst_url.data(), &addr)) {
                poller->async([args, cb]() {
                    //切回自己的线程
                    cb(0, SockException(Err_dns, StrPrinter << "dns解析域名失败:" << args.dst_url));
                });
                return;
            }
            sockaddr_set_port(&addr, args.dst_port);
            //dns解析成功
            poller->async([args, addr, weak_self, cb]() {
                //切回自己的线程
                auto strong_self = weak_self.lock();
                if (!strong_self) {
                    return;
                }
                string ifr_ip = addr.sa.sa_family == AF_INET ? "0.0.0.0" : "::";
                try {
                    if (args.src_port) {
                        //指定端口
                        hio_t* io = hloop_create_udp_server(hv::tlsEventLoop()->loop(), ifr_ip.data(), args.src_port);
                        if (!io) {
                            throw std::invalid_argument(StrPrinter << "bindUdpSock failed on port:" << args.src_port);
                        }
                        strong_self->_socket_rtp = std::make_shared<toolkit::Session>(io);
                    } else {
                        toolkit::SessionPtr pr[2];
                        //从端口池获取随机端口
                        makeSockPair(pr, ifr_ip, true);
                        strong_self->_socket_rtp = pr[0];
                    }
                } catch (std::exception &ex) {
                    cb(0, SockException(Err_other, ex.what()));
                    return;
                }
                hio_set_peeraddr(strong_self->_socket_rtp->io(), (struct sockaddr *)&addr, sockaddr_len(&addr));
                strong_self->onConnect();
                cb(strong_self->_socket_rtp->get_local_port(), SockException());
            });
        });
    } else {
        hio_t* io = hio_create_socket(_poller->loop(), "::", args.src_port);
        _socket_rtp = std::make_shared<toolkit::Session>(io);
        _socket_rtp->onconnect = [cb, weak_self]() {
            SockException err;
            auto strong_self = weak_self.lock();
            if (strong_self) {
                if (!err) {
                    //tcp连接成功
                    strong_self->onConnect();
                }
                cb(strong_self->_socket_rtp->get_local_port(), err);
            } else {
                cb(0, err);
            }
        };//, 5.0F, "::", args.src_port);
        _socket_rtp->setConnectTimeout(5000);
        _socket_rtp->onclose = [cb, weak_self]() {
            auto err = SockException(Err_eof, "socket closed");
            auto strong_self = weak_self.lock();
            if (strong_self) {
                cb(strong_self->_socket_rtp->get_local_port(), err);
            }
        };
        // connect(args.dst_url, args.dst_port , 5.0F, "::", args.src_port);
        _socket_rtp->startConnect(args.dst_port, args.dst_url.c_str());
    }
}

void RtpSender::createRtcpSocket() {
    if (_socket_rtcp) {
        return;
    }
    //rtcp端口使用户rtp端口+1
    hio_t* io = hloop_create_udp_server(_poller->loop(), 
        _socket_rtp->get_local_ip().c_str(), _socket_rtp->get_local_port() + 1);
    if (!io) {
        WarnL << "bind rtcp udp socket failed";
        return ;
    }
    _socket_rtcp = std::make_shared<toolkit::Session>(io);

    //目标rtp端口
    sockaddr_u addr = *(sockaddr_u*)hio_peeraddr(_socket_rtp->io());
    //绑定目标rtcp端口(目标rtp端口 + 1)
    sockaddr_set_port(&addr, sockaddr_port(&addr)+1);
    hio_set_peeraddr(io, (sockaddr*)&addr, sockaddr_len(&addr));

    _rtcp_context = std::make_shared<RtcpContextForSend>();
    weak_ptr<RtpSender> weak_self = shared_from_this();
    _socket_rtcp->onread = [weak_self](hv::Buffer* buf) {
        //接收receive report rtcp
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        auto rtcp_arr = RtcpHeader::loadFromBytes((char*)buf->data(), buf->size());
        for (auto &rtcp : rtcp_arr) {
            strong_self->onRecvRtcp(rtcp);
        }
    };
    InfoL << "open rtcp port success, start check rr rtcp timeout";
}

void RtpSender::onRecvRtcp(RtcpHeader *rtcp) {
    _rtcp_context->onRtcp(rtcp);
    _rtcp_recv_ticker.resetTime();
}

//连接建立成功事件
void RtpSender::onConnect(){
    _is_connect = true;
    //加大发送缓存,防止udp丢包之类的问题
    so_rcvbuf(_socket_rtp->fd(), 4 * 1024 * 1024);
    if (!_args.is_udp) {
        //关闭tcp no_delay并开启MSG_MORE, 提高发送性能
        tcp_nodelay(_socket_rtp->fd(), false);
        //_socket_rtp->setSendFlags(SOCKET_DEFAULE_FLAGS | FLAG_MORE);
    } else if (_args.udp_rtcp_timeout) {
        createRtcpSocket();
    }
    
    //连接建立成功事件
    std::weak_ptr<RtpSender> weak_self = shared_from_this();
    _socket_rtp->onclose = [weak_self]() {
        if (auto strong_self = weak_self.lock())
            strong_self->onErr(SockException(Err_eof, "socket closed"));
    };
    //获取本地端口，断开重连后确保端口不变
    _args.src_port = _socket_rtp->get_local_port();
    InfoL << "开始发送 rtp:" << _socket_rtp->get_peer_ip() << ":" << _socket_rtp->get_peer_port() << ", 是否为udp方式:" << _args.is_udp;
}

bool RtpSender::addTrack(const Track::Ptr &track){
    return _interface->addTrack(track);
}

void RtpSender::addTrackCompleted(){
    _interface->addTrackCompleted();
}

void RtpSender::resetTracks(){
    _interface->resetTracks();
}

void RtpSender::flush() {
    if (_interface) {
        _interface->flush();
    }
}

//此函数在其他线程执行
bool RtpSender::inputFrame(const Frame::Ptr &frame) {
    //连接成功后才做实质操作(节省cpu资源)
    return _is_connect ? _interface->inputFrame(frame) : false;
}

void RtpSender::onSendRtpUdp(const toolkit::Buffer::Ptr &buf, bool check) {
    if (!_socket_rtcp) {
        return;
    }
    auto rtp = static_pointer_cast<RtpPacket>(buf);
    _rtcp_context->onRtp(rtp->getSeq(), rtp->getStamp(), rtp->getStampMS(), 90000 /*not used*/, rtp->size());

    if (!check) {
        //减少判断次数
        return;
    }
    //每5秒发送一次rtcp
    if (_rtcp_send_ticker.elapsedTime() > _args.rtcp_send_interval_ms) {
        _rtcp_send_ticker.resetTime();
        //rtcp ssrc为rtp ssrc + 1
       auto sr = _rtcp_context->createRtcpSR(atoi(_args.ssrc.data()) + 1);
       //send sender report rtcp
       _socket_rtcp->write(sr->data(), sr->size());
    }

    if (_rtcp_recv_ticker.elapsedTime() > _args.rtcp_timeout_ms) {
        //接收rr rtcp超时
        WarnL << "recv rr rtcp timeout";
        _rtcp_recv_ticker.resetTime();
        onClose(SockException(Err_timeout, "recv rr rtcp timeout"));
    }
}

void RtpSender::onClose(const SockException &ex) {
    auto cb = _on_close;
    if (cb) {
        //在下次循环时触发onClose，原因是防止遍历map时删除元素
        _poller->async([cb, ex]() { cb(ex); }, false);
    }
}

//此函数在其他线程执行
void RtpSender::onFlushRtpList(shared_ptr<std::list<Buffer::Ptr> > rtp_list) {
    if(!_is_connect){
        //连接成功后才能发送数据
        return;
    }


    size_t i = 0;
    auto size = rtp_list->size();
    for (auto packet : *rtp_list) {
        if (_args.is_udp) {
            onSendRtpUdp(packet, i == 0);
            //udp模式，rtp over tcp前4个字节可以忽略
            _socket_rtp->write(packet->data() + 4, packet->size() - 4);
        } else {
            // tcp模式, rtp over tcp前2个字节可以忽略,只保留后续rtp长度的2个字节
            _socket_rtp->write(packet->data() + 2, packet->size() - 2);
        }
    }
}

void RtpSender::onErr(const SockException &ex) {
    _is_connect = false;
    WarnL << "send rtp connection lost: " << ex.what();
    onClose(ex);
}

void RtpSender::setOnClose(std::function<void(const toolkit::SockException &ex)> on_close){
    _on_close = std::move(on_close);
}

}//namespace mediakit
#endif// defined(ENABLE_RTPPROXY)