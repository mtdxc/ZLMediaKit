﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SRC_HTTP_HTTPDOWNLOADER_H_
#define SRC_HTTP_HTTPDOWNLOADER_H_

#include "HttpClientImp.h"

namespace mediakit {

class HttpDownloader : public HttpClientImp {
public:
    using Ptr = std::shared_ptr<HttpDownloader>;
    using onDownloadResult = std::function<void(const toolkit::SockException &ex, const std::string &filePath)>;

    HttpDownloader() = default;
    ~HttpDownloader() override;

    /**
     * 开始下载文件,默认断点续传方式下载
     * @param url 下载http url
     * @param file_path 文件保存地址，置空则选择默认文件路径: HttpDownloader/Md5(Url)
     * @param append 如文件已存在，是否启用断点续传
     */
    void startDownload(const std::string &url, const std::string &file_path = "", bool append = false);

    void startDownload(const std::string &url, const onDownloadResult &cb) {
        setOnResult(cb);
        startDownload(url, "", false);
    }

    void setOnResult(const onDownloadResult &cb) { _on_result = cb; }
    // 进度回调
    using onProgressCallback = std::function<void(size_t cur, size_t total)>;
    void setOnProgress(const onProgressCallback& cb) { _on_progress = cb; }
protected:
    void onResponseBody(const char *buf, size_t size) override;
    void onResponseHeader(const std::string &status, const HttpHeader &headers) override;
    void onResponseCompleted(const toolkit::SockException &ex) override;

private:
    void closeFile();

private:
    FILE *_save_file = nullptr;
    std::string _file_path;
    onDownloadResult _on_result;

    onProgressCallback _on_progress;
    size_t _start_pos = 0;
};

} /* namespace mediakit */

#endif /* SRC_HTTP_HTTPDOWNLOADER_H_ */
