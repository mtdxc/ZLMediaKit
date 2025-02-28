﻿/*
 * Copyright (c) 2020 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdlib>
#include <cinttypes>
#include "HlsParser.h"
#include "Util/util.h"
#include "Common/Parser.h"

using std::string;
using namespace toolkit;

namespace mediakit {
/* @see http://tools.ietf.org/html/draft-pantos-http-live-streaming-06
https://blog.csdn.net/kl222/article/details/14526031

多码流m3u8:
#EXTM3U
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=1280000
http://example.com/low.m3u8
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=2560000
http://example.com/mid.m3u8
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=7680000
http://example.com/hi.m3u8
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=65000,CODECS="mp4a.40.5"
http://example.com/audio-only.m3u8

单码流m3u8:
#EXTM3U
#EXT-X-VERSION:3
#EXT-X-ALLOW-CACHE:NO
#EXT-X-TARGETDURATION:3
#EXT-X-MEDIA-SEQUENCE:19
#EXTINF:1.984,
2022-03-08/09/41-25_19.ts
#EXTINF:2.048,
2022-03-08/09/41-27_20.ts
#EXTINF:1.984,
2022-03-08/09/41-29_21.ts
#EXT-X-ENDLIST
*/
bool HlsParser::parse(const string &http_url, const string &m3u8) {
    float extinf_dur = 0;
    ts_segment segment;
    std::map<int, ts_segment> ts_map;
    _total_dur = 0;
    _is_live = true;
    _is_m3u8_inner = false;
    int index = 0;

    string root_url = http_url.substr(0, http_url.find("/", 8));
    string parent_url = http_url.substr(0, http_url.rfind("/") + 1);
    auto lines = split(m3u8, "\n");
    for (auto &line : lines) {
        trim(line);
        if (line.size() < 2) {
            continue;
        }

        if (line[0] != '#' && (_is_m3u8_inner || extinf_dur != 0)) {
            segment.duration = extinf_dur;
            if (line.find("http://") == 0 || line.find("https://") == 0) 
            {// http绝对路径
                segment.url = line;
            } else {
                if (line[0] == '/') { // 根路径
                    segment.url = root_url + line;
                } else { // 相对路径
                    segment.url = parent_url + line;
                }
            }
            if (!_is_m3u8_inner) {
                //ts按照先后顺序排序
                ts_map.emplace(index++, segment);
            } else {
                //子m3u8按照带宽排序
                ts_map.emplace(segment.bandwidth, segment);
            }
            extinf_dur = 0;
            continue;
        }

        _is_m3u8_inner = false;
        if (line.find("#EXTINF:") == 0) {
            sscanf(line.data(), "#EXTINF:%f,", &extinf_dur);
            _total_dur += extinf_dur;
            continue;
        }
        static const string s_stream_inf = "#EXT-X-STREAM-INF:";
        if (line.find(s_stream_inf) == 0) {
            _is_m3u8_inner = true;
            auto key_val = Parser::parseArgs(line.substr(s_stream_inf.size()), ",", "=");
            segment.program_id = atoi(key_val["PROGRAM-ID"].data());
            segment.bandwidth = atoi(key_val["BANDWIDTH"].data());
            sscanf(key_val["RESOLUTION"].data(), "%dx%d", &segment.width, &segment.height);
            continue;
        }

        if (line == "#EXTM3U") {
            _is_m3u8 = true;
            continue;
        }

        if (line.find("#EXT-X-ALLOW-CACHE:") == 0) {
            _allow_cache = (line.find(":YES") != string::npos);
            continue;
        }

        if (line.find("#EXT-X-VERSION:") == 0) {
            sscanf(line.data(), "#EXT-X-VERSION:%d", &_version);
            continue;
        }

        if (line.find("#EXT-X-TARGETDURATION:") == 0) {
            sscanf(line.data(), "#EXT-X-TARGETDURATION:%d", &_target_dur);
            continue;
        }

        if (line.find("#EXT-X-MEDIA-SEQUENCE:") == 0) {
            sscanf(line.data(), "#EXT-X-MEDIA-SEQUENCE:%" PRId64, &_sequence);
            continue;
        }

        if (line.find("#EXT-X-ENDLIST") == 0) {
            //点播
            _is_live = false;
            continue;
        }
    }

    if (_is_m3u8) {
        onParsed(_is_m3u8_inner, _sequence, ts_map);
    }
    return _is_m3u8;
}

}//namespace mediakit
