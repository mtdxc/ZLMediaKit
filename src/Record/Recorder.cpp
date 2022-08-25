/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Recorder.h"
#include "Common/config.h"
#include "Common/MediaSource.h"
#include "Util/File.h"
#include "MP4Recorder.h"
#include "Hls/HlsRecorder.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

string Recorder::getRecordPath(Recorder::type type, const string &vhost, const string &app, const string &stream_id, const string &customized_path) {
    GET_CONFIG(bool, enableVhost, General::kEnableVhost);
    switch (type) {
        case Recorder::type_hls: {
            GET_CONFIG(string, hlsPath, Protocol::kHlsSavePath);
            string m3u8FilePath;
            if (enableVhost) {
                m3u8FilePath = vhost + "/" + app + "/" + stream_id + "/hls.m3u8";
            } else {
                m3u8FilePath = app + "/" + stream_id + "/hls.m3u8";
            }
            return File::absolutePath(m3u8FilePath, customized_path.empty() ? hlsPath : customized_path);
        }
        case Recorder::type_mp4: {
            GET_CONFIG(string, recordPath, Protocol::kMP4SavePath);
            GET_CONFIG(string, recordAppName, Record::kAppName);
            string mp4FilePath;
            if (enableVhost) {
                mp4FilePath = vhost + "/" + recordAppName + "/" + app + "/" + stream_id + "/";
            } else {
                mp4FilePath = recordAppName + "/" + app + "/" + stream_id + "/";
            }
            return File::absolutePath(mp4FilePath, customized_path.empty() ? recordPath : customized_path);
        }
        default:
            return "";
    }
}

std::shared_ptr<MediaSinkInterface> Recorder::createRecorder(type type, const string &vhost, const string &app, const string &stream_id, const ProtocolOption &option){
    switch (type) {
        case Recorder::type_hls: {
#if defined(ENABLE_HLS)
            auto path = Recorder::getRecordPath(type, vhost, app, stream_id, option.hls_save_path);
            GET_CONFIG(bool, enable_vhost, General::kEnableVhost);
            auto ret = std::make_shared<HlsRecorder>(path, enable_vhost ? string(VHOST_KEY) + "=" + vhost : "", option);
            ret->setMediaSource(vhost, app, stream_id);
            return ret;
#else
            throw std::invalid_argument("hls相关功能未打开，请开启ENABLE_HLS宏后编译再测试");
#endif

        }

        case Recorder::type_mp4: {
#if defined(ENABLE_MP4)
            auto path = Recorder::getRecordPath(type, vhost, app, stream_id, option.mp4_save_path);
            return std::make_shared<MP4Recorder>(path, vhost, app, stream_id, option.mp4_max_second);
#else
            throw std::invalid_argument("mp4相关功能未打开，请开启ENABLE_MP4宏后编译再测试");
#endif
        }

        default: throw std::invalid_argument("未知的录制类型");
    }
}

} /* namespace mediakit */
