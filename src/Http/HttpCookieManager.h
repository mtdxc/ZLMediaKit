﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SRC_HTTP_COOKIEMANAGER_H
#define SRC_HTTP_COOKIEMANAGER_H

#include "Common/Parser.h"
//#include "Network/Socket.h"
#include "Poller/Timer.h"
#include "Util/TimeTicker.h"
#include "Util/mini.h"
#include "Util/util.h"
#include <memory>
#include <unordered_map>

#define COOKIE_DEFAULT_LIFE (7 * 24 * 60 * 60)

namespace mediakit {

class HttpCookieManager;

/**
 * cookie对象，用于保存cookie的一些相关属性
 */
class HttpServerCookie : public toolkit::noncopyable {
public:
    using Ptr = std::shared_ptr<HttpServerCookie>;
    /**
     * 构建cookie
     * @param manager cookie管理者对象
     * @param cookie_name cookie名，例如MY_SESSION
     * @param uid 用户唯一id
     * @param cookie cookie随机字符串
     * @param max_elapsed 最大过期时间，单位秒
     */

    HttpServerCookie(const std::shared_ptr<HttpCookieManager> &manager, 
        const std::string &name, 
        const std::string &uid,
        const std::string &value, 
        uint64_t max_elapsed);
    ~HttpServerCookie();

    const std::string &getUid() const { return _uid; }
    const std::string &getValue() const { return _value; }
    const std::string &getName() const { return _name; }

    /**
     * 获取http中Set-Cookie字段的值
     * @param cookie_name 该cookie的名称，譬如 MY_SESSION
     * @param path http访问路径
     * @return 例如 MY_SESSION=XXXXXX;expires=Wed, Jun 12 2019 06:30:48 GMT;path=/index/files/
     */
    std::string getCookie(const std::string &path) const;

    /**
     * 更新cookie过期时间，可让此cookie不失效
     */
    void updateTime();

    /**
     * 判断该cookie是否过期
     */
    bool isExpired();

    /**
     * 设置附加数据
     */
    void setAttach(std::shared_ptr<void> attach) {
        _attach = std::move(attach);
    }

    /*
     * 获取附加数据
     */
    template <class T>
    T& getAttach() {
        return *static_cast<T *>(_attach.get());
    }

private:
    std::string cookieExpireTime() const;

private:
    std::string _uid;
    std::string _name;
    std::string _value;
    uint64_t _max_elapsed;
    toolkit::Ticker _ticker;
    std::shared_ptr<void> _attach;
    std::weak_ptr<HttpCookieManager> _manager;
};

/**
 * cookie随机字符串生成器
 */
class RandStrGenerator {
public:
    RandStrGenerator() = default;
    ~RandStrGenerator() = default;

    /**
     * 获取不碰撞的随机字符串
     * @return 随机字符串
     */
    std::string obtain();

    /**
     * 释放随机字符串
     * @param str 随机字符串
     */
    void release(const std::string &str);

private:
    std::string obtain_l();

private:
    //碰撞库
    std::unordered_set<std::string> _obtained;
    //增长index，防止碰撞用
    int _index = 0;
};

/**
 * cookie管理器，用于管理cookie的生成以及过期管理，同时实现了同账号异地挤占登录功能
 * 在本类中cookie的value是用RandStrGeneator生成的全局唯一的字符串，并不关心值内容，只关心其唯一性
 * 该对象限制同账号最多登录若干个设备
 */
class HttpCookieManager : public std::enable_shared_from_this<HttpCookieManager> {
public:
    friend class HttpServerCookie;
    using Ptr =  std::shared_ptr<HttpCookieManager>;
    ~HttpCookieManager();

    /**
     *  获取单例
     */
    static HttpCookieManager &Instance();

    /**
     * 添加cookie, 并返回cookie对象
     */
    HttpServerCookie::Ptr addCookie(
        const std::string &cookie_name, ///< cookie名称,如MY_SESSION
        const std::string &uid, ///< 用户id，如果为空则为匿名登录 
        uint64_t max_elapsed = COOKIE_DEFAULT_LIFE, ///< 过期时间，单位秒
        std::shared_ptr<void> attach = nullptr, ///< 附加用户数据
        int max_client = 1 ///< uid最多可登录的设备个数
    );

    /**
     * 根据cookie随机字符串查找cookie对象
     * @param cookie_name cookie名，例如MY_SESSION
     * @param cookie cookie随机字符串
     * @return cookie对象，可以为nullptr
     */
    HttpServerCookie::Ptr getCookie(const std::string &cookie_name, const std::string &cookie);

    /**
     * 从http头中获取cookie对象
     Cookie: MY_SESSION=XXXXXX;expires=Wed, Jun 12 2019 06:30:48 GMT;path=/index/files/
     * @param cookie_name cookie名，例如MY_SESSION
     * @param http_header http头
     * @return cookie对象
     */
    HttpServerCookie::Ptr getCookie(const std::string &cookie_name, const StrCaseMap &http_header);

    /**
     * 根据uid获取cookie
     * @param cookie_name cookie名，例如MY_SESSION
     * @param uid 用户id
     * @return cookie对象
     */
    HttpServerCookie::Ptr getCookieByUid(const std::string &cookie_name, const std::string &uid);

    /**
     * 删除cookie，用户登出时使用
     * @param cookie cookie对象，可以为nullptr
     * @return
     */
    bool delCookie(const HttpServerCookie::Ptr &cookie);

private:
    HttpCookieManager();

    void onManager();
    /**
     * 构造cookie对象时触发，目的是记录某账号下多个cookie
     * @param cookie_name cookie名，例如MY_SESSION
     * @param uid 用户id
     * @param cookie cookie随机字符串
     */
    void onAddCookie(const std::string &cookie_name, const std::string &uid, const std::string &cookie);

    /**
     * 析构cookie对象时触发
     * @param cookie_name cookie名，例如MY_SESSION
     * @param uid 用户id
     * @param cookie cookie随机字符串
     */
    void onDelCookie(const std::string &cookie_name, const std::string &uid, const std::string &cookie);

    /**
     * 获取某用户名下最先登录时的cookie，目的是实现某用户下最多登录若干个设备
     * @param cookie_name cookie名，例如MY_SESSION
     * @param uid 用户id
     * @param max_client 最多登录的设备个数
     * @return 最早的cookie随机字符串
     */
    std::string getOldestCookie(const std::string &cookie_name, const std::string &uid, int max_client = 1);

    /**
     * 删除cookie
     * @param cookie_name cookie名，例如MY_SESSION
     * @param cookie cookie随机字符串
     * @return 成功true
     */
    bool delCookie(const std::string &cookie_name, const std::string &cookie);

private:
    std::unordered_map<std::string /*cookie_name*/, std::unordered_map<std::string /*cookie*/, HttpServerCookie::Ptr /*cookie_data*/>> _map_cookie;
    // 用于记录某个uid产生的多个cookie (max_client)
    std::unordered_map<std::string /*cookie_name*/, std::unordered_map<std::string /*uid*/, std::map<uint64_t /*cookie create timestamp*/, std::string /*cookie*/>>> _map_uid_to_cookie;
    std::recursive_mutex _mtx_cookie;
    toolkit::Timer::Ptr _timer;
    RandStrGenerator _generator;
};

} // namespace mediakit

#endif // SRC_HTTP_COOKIEMANAGER_H
