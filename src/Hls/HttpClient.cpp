/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdlib>
#include "HttpClient.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

void HttpClient::sendRequest(const std::string &url) {
    _req->url = url;
    //req->timeout = 3600; // 1h
    _req->http_cb = [this]
    (HttpMessage* resp, http_parser_state state, const char* data, size_t size) {
        switch (state)
        {
        case HP_START_REQ_OR_RES:
            break;
        case HP_MESSAGE_BEGIN:
            _waitResp = true;
            break;
        case HP_URL:
            break;
        case HP_STATUS:
            break;
        case HP_HEADER_FIELD:
            break;
        case HP_HEADER_VALUE:
            break;
        case HP_HEADERS_COMPLETE:
            onResponseHeader(std::to_string(((HttpResponse*)resp)->status_code), resp->headers);
            break;
        case HP_CHUNK_HEADER:
            break;
        case HP_BODY:
            onResponseBody(data, size);
            break;
        case HP_CHUNK_COMPLETE:
            break;
        case HP_MESSAGE_COMPLETE:
        case HP_ERROR: {
            _waitResp = false;
            toolkit::SockException ex;
            onResponseCompleted(ex);
        }
            break;
        default:
            break;
        }
    };
    _http = std::make_shared<hv::AsyncHttpClient>(_loop);
    _http->send(_req, [this](const HttpResponsePtr& resp) {
        _resp = resp;
    });
}

void HttpClient::clear() {
    _req->Reset();
    clearResponse();
}

void HttpClient::clearResponse() {
    _waitResp = false;
    _resp = nullptr;
}

const string &HttpClient::getUrl() const {
    return _req->url;
}

bool HttpClient::waitResponse() {
    return _waitResp;
}
void HttpClient::setHeaderTimeout(size_t timeout_ms) {
    // CHECK(timeout_ms > 0);
    // _wait_header_ms = timeout_ms;
    _req->connect_timeout = timeout_ms / 1000;
}

void HttpClient::setBodyTimeout(size_t timeout_ms) {
    _wait_body_ms = timeout_ms;
}

void HttpClient::setCompleteTimeout(size_t timeout_ms) {
    //_wait_complete_ms = timeout_ms;
    _req->timeout = timeout_ms / 1000;
}

void HttpClient::shutdown(toolkit::SockException ex)
{
    _http = nullptr;
    clearResponse();
}

} /* namespace mediakit */
