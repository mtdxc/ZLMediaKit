/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef Http_HttpClient_h
#define Http_HttpClient_h

#include <stdio.h>
#include <string.h>
#include <functional>
#include <memory>
#include "AsyncHttpClient.h"
#include "Util/mini.h"
#include "toolkit.h"


namespace mediakit {

class HttpArgs : public std::map<std::string, toolkit::variant, hv::StringCaseLess> {
public:
    HttpArgs() = default;
    ~HttpArgs() = default;
    // make http query string
    std::string make() const {
        std::string ret;
        for (auto &pr : *this) {
            ret.append(pr.first);
            ret.append("=");
            //ret.append(strCoding::UrlEncode(pr.second));
            ret.append("&");
        }
        if (ret.size()) {
            ret.pop_back();
        }
        return ret;
    }
};
/*
Http请求基类，支持:
- 设置请求/header、body超时
- cookie管理：请求时携带cookie，响应自动存储cookie
- 连接复用: 当地址一样时，复用上次Tcp连接
- 支持basic auth验证
- http 301/302 url重定向
- MultiPart表单请求
- chunk编码接收
@note 该类body接收采用多次分段接收, onResponseBody有数据就会回调，而不是等缓存完毕后再一次回调
*/
class HttpClient : public std::enable_shared_from_this<HttpClient> {
public:
    using HttpHeader = http_headers;
    using Ptr = std::shared_ptr<HttpClient>;

    HttpClient() { _req = std::make_shared<HttpRequest>(); }
    virtual ~HttpClient() = default;


    /**
     * 发送http[s]请求
     * @param url 请求url
     */
    virtual void sendRequest(const std::string &url);

    /**
     * 重置对象
     */
    virtual void clear();

    /**
     * 设置http方法
     * @param method GET/POST等
     */
    void setMethod(std::string method) {
        return _req->SetMethod(method.c_str());
    }

    /**
     * 覆盖http头
     * @param header
     */
    void setHeader(http_headers header) {
        _req->headers = header;
    }

    /**
    @brief 添加Http头部.
    
    @param key 头部名
    @param val 头部值
    @param force 存在是否覆盖
    @return this引用，用于链式操作
    */
    HttpClient &addHeader(std::string key, std::string val, bool force = false) {
        _req->SetHeader(key.c_str(), val.c_str());
        return *this;
    }

    /**
     * 设置http content
     * @param body http content
     */
    void setBody(std::string body) {
        _req->SetBody(body);
    }

    /**
     * 获取回复，在收到完整回复(_complete=true)后有效
     */
    const HttpResponsePtr &response() const { return _resp;}

    /**
     * 获取Content-Length返回的body大小
     */
    //ssize_t responseBodyTotalSize() const;

    /**
     * 获取已经下载body的大小
     */
    //size_t responseBodySize() const;

    /**
     * 获取请求url
     */
    const std::string &getUrl() const;

    /**
     * 判断是否正在等待响应
     */
    bool waitResponse();

    /**
     * 判断是否为https
     */
    bool isHttps() const { return _req->IsHttps(); }

    /**
     * 设置从发起连接到接收header完毕的延时，默认10秒
     * 此参数必须大于0
     */
    void setHeaderTimeout(size_t timeout_ms);

    /**
     * 设置接收body数据超时时间, 默认5秒
     * 此参数可以用于处理超大body回复的超时问题
     * 此参数可以等于0
     */
    void setBodyTimeout(size_t timeout_ms);

    /**
     * 设置整个连接超时超时时间, 默认0
     * 当设置该值后(!=0)，HeaderTimeout和BodyTimeout无效
     */
    void setCompleteTimeout(size_t timeout_ms);

    void setNetAdapter(std::string name) {}
    void shutdown(toolkit::SockException ex);
    void setLoop(toolkit::EventPollerPtr loop) { _loop = loop; }
    toolkit::EventPollerPtr loop() { return _loop; }
protected:
    toolkit::EventPollerPtr _loop;
    /**
     * 收到http响应头
     * @param status 状态码，譬如:200 OK
     * @param headers http响应头部Map
     */
    virtual void onResponseHeader(const std::string &status, const HttpHeader &headers) = 0;

    /**
     * 收到http content数据
     * 这里采用分段接收，收到数据后就回调，而不是等Content-Length接收后才回调；
     * 对于chunk编码，则是收到一个chunk就回调
     * @param buf 数据指针
     * @param size 数据大小
     */
    virtual void onResponseBody(const char *buf, size_t size) = 0;

    /**
     * 接收http响应完毕,
     */
    virtual void onResponseCompleted(const toolkit::SockException &ex) = 0;

    /**
     * 重定向事件
     * @param url 重定向url
     * @param temporary 是否为临时重定向 301 or 302
     * @return 是否继续
     */
    virtual bool onRedirectUrl(const std::string &url, bool temporary) { return true; };

protected:
    void clearResponse();

private:
    //for http response
    bool _waitResp = false;
    HttpRequestPtr _req;
    HttpResponsePtr _resp;
    std::shared_ptr<hv::AsyncHttpClient> _http;
    //for timeout
    size_t _wait_header_ms = 10 * 1000;
    size_t _wait_body_ms = 10 * 1000;
    size_t _wait_complete_ms = 0;
};

} /* namespace mediakit */

#endif /* Http_HttpClient_h */
