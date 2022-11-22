/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <ctime>
#include <sys/stat.h>
#include "HlsMakerImp.h"
#include "Util/util.h"
#include "Util/uv_errno.h"
#include "Util/File.h"
#include "Common/config.h"
#include "Camera/NvrRecord.h"
#include "TS/Decoder.h"
#include "EventLoopThreadPool.h"
#include "HlsMediaSource.h"

#if defined(_WIN32)
//#include "Util/strptime_win.h"
char* strptime(const char *buf, const char *fmt, struct tm *tm)
{return nullptr;}
#endif

using namespace std;
using namespace toolkit;

namespace mediakit {

//@ todo ts->mp4
bool AppendFile(DecoderImp* decoder, const std::string& path){
    bool ret = false;
    unsigned char buf[188];
    FILE* fpSrc = File::create_file(path.c_str(), "rb");
    if(!fpSrc) return ret;
    ret = true;
    while(1){
        int n = fread(buf, 1, sizeof(buf), fpSrc);
        if(n<=0) break;
        decoder->input(buf, n);
    }
    fclose(fpSrc);
    return ret;
}

long AppendFile(FILE* fp, const std::string& path) {
    long ret = 0;
    FILE* fpSrc = File::create_file(path.c_str(), "rb");
    if (fpSrc) {
        char buf[8192];
        while(1) {
            int n = fread(buf, 1, sizeof(buf), fpSrc);
            if (n<=0) break;
            ret += fwrite(buf, 1, n, fp);
        }
        fclose(fpSrc);
    }
    else {
        WarnL << "unable to open " << path;
    }
    return ret;
}

void SnapTask::DoTask() {
    status = 0; error.clear();
    InfoL << "merge " << files.size() << " files to " << dstFile;
    if (files.empty() || dstFile.empty()) {
        Notify(-1, "nofiles");
        return ;
    }
    long ret = 0;
    if (end_with(dstFile, ".ts")) {
        std::shared_ptr<FILE> muxer(File::create_file(dstFile.data(), "wb"), [](FILE *fp) {if (fp) fclose(fp); });
        if (!muxer) {
            Notify(-2, "unable to open dstFile");
            return;
        }
        for (auto file : files)
            ret += AppendFile(muxer.get(), file);
    }
    else {
        try {
            RecordSink snap(dstFile);
            auto muxer = DecoderImp::createDecoder(DecoderImp::decoder_ts, &snap);
            for (auto file : files)
                ret += AppendFile(muxer.get(), file);
        }
        catch (std::exception&) {
            Notify(-2, "unable to open dstFile");
            return;
        }
    }

    if (ret > 0)
        Notify(1, "");
    else
        Notify(-3, "file empty");

}


void SnapTask::Notify(int code, const char* err) {
    status = code;
    error = err;
    InfoL << "done with " << status << ": " << err;

    bool result = code > 0;
    cb(result, result ? dstFile : error);
}

HlsMakerImp::HlsMakerImp(const string &m3u8_file,
                         const string &params,
                         uint32_t bufSize,
                         float seg_duration,
                         uint32_t seg_number,
                         bool seg_keep):HlsMaker(seg_duration, seg_number, seg_keep) {
    _poller = hv::EventLoopThreadPool::Instance()->loop();
    _path_prefix = File::parentDir(m3u8_file);
    _path_hls = m3u8_file;
    _params = params;
    _buf_size = bufSize;
    _file_buf.reset(new char[bufSize], [](char *ptr) {
        delete[] ptr;
    });

    _info.folder = _path_prefix;
    _duration = 0;
}

HlsMakerImp::~HlsMakerImp() {
    clearCache(false, true);
}

void HlsMakerImp::clearCache() {
    clearCache(true, false);
}

void HlsMakerImp::clearCache(bool immediately, bool eof) {
    //录制完了
    flushLastSegment(eof);
    if (!isLive()||isKeep()) {
        return;
    }
    InfoL << immediately << ", eof=" << eof;
    clear();
    _file = nullptr;
    _segment_file_paths.clear();
    _duration = 0;

    //hls直播才删除文件
    GET_CONFIG(uint32_t, delay, Hls::kDeleteDelaySec);
    if (!delay || immediately) {
        File::delete_file(_path_prefix.data());
    } else {
        auto path_prefix = _path_prefix;
        _poller->doDelayTask(delay * 1000, [path_prefix]() {
            File::delete_file(path_prefix.data());
            return 0;
        });
    }
}
#define TIME_DIR_FMT "%Y-%m-%d/%H/%M-%S"
// 根据文件名解析创建时间和索引
bool HlsMakerImp::parseFileName(std::string seg_path, time_t& time, int64_t* index){
    if (start_with(seg_path, _path_prefix))
        seg_path = seg_path.substr(_path_prefix.length() + 1);

    struct tm tm{0};
    char* p = strptime(seg_path.c_str(), TIME_DIR_FMT, &tm);
    if (!p)
        return false;
    time = mktime(&tm);
    auto pos = seg_path.rfind('_');
    if (-1 == pos)
        return false;
    if (index) {
        return sscanf(seg_path.data() + pos + 1, "%I64u.ts", index);
    }
    return true;
}

bool HlsMakerImp::lookupFile(uint64_t index, std::string& file){
    file.clear();
    auto it = _segment_file_paths.find(index);
    if (it == _segment_file_paths.end()) {
        // 通过索引来查找文件
        toolkit::_StrPrinter end;
        end << "_" << index << ".ts";
        File::scanDir(_path_prefix, [&end, &file](const string &path, bool isDir) -> bool {
            if (!isDir && end_with(path, end)) {
                file = path;
                return false;
            }
            return true;
        }, true);
    }
    else{
        file = it->second.filename;
    }
    return !file.empty();
}

string HlsMakerImp::onOpenSegment(uint64_t index) {
    string segment_name, segment_path;
    {
        segment_name = StrPrinter << getTimeStr(TIME_DIR_FMT) << "_" << index << ".ts";
        segment_path = _path_prefix + "/" + segment_name;
        if (isLive()) {
            TsItem ti = {0, segment_path};
            _segment_file_paths.emplace(index, ti);
        }
    }
    _file = makeFile(segment_path, true);

    //保存本切片的元数据
    _info.start_time = ::time(NULL);
    _info.file_name = segment_name;
    _info.file_path = segment_path;
    _info.url = _info.app + "/" + _info.stream + "/" + segment_name;

    if (!_file) {
        WarnL << "create file failed," << segment_path << " " << get_uv_errmsg();
    }
    if (_params.empty()) {
        return segment_name;
    }
    return segment_name + "?" + _params;
}

void HlsMakerImp::onDelSegment(uint64_t index) {
    auto it = _segment_file_paths.find(index);
    if (it == _segment_file_paths.end()) {
        return;
    }
    _duration -= it->second.duration / 1000.0f;
    File::delete_file(it->second.filename.c_str());
    _segment_file_paths.erase(it);
}

void HlsMakerImp::onWriteSegment(const char *data, size_t len) {
    if (_file) {
        fwrite(data, len, 1, _file.get());
    }
    if (_media_src) {
        _media_src->onSegmentSize(len);
    }
}

void HlsMakerImp::onWriteHls(const std::string &data) {
    auto hls = makeFile(_path_hls);
    if (hls) {
        fwrite(data.data(), data.size(), 1, hls.get());
        hls.reset();
        if (_media_src) {
            _media_src->setIndexFile(data);
        }
    } else {
        WarnL << "create hls file failed," << _path_hls << " " << get_uv_errmsg();
    }
    //DebugL << "\r\n"  << string(data,len);
}

void HlsMakerImp::makeSnap(int maxSec, const std::string& prefix, std::function<void (bool, std::string)> cb) {
    SnapTask::Ptr task = std::make_shared<SnapTask>();
	task->cb = cb;
    
    std::string path;
    int64_t index = current_index();
    time_t ftime = 0, now = ::time(NULL);
    time_t stime = now - maxSec;
    InfoL << "start with " << index << ", end_time " << stime << ", maxSec=" << maxSec;
    for (; index >=0; index--) {
        if (!lookupFile(index, path)) {
            InfoL << "break loop at " << index << " without file";
            break;
        }
        if (parseFileName(path, ftime, nullptr)){
            task->files.push_front(path);
            if (ftime < stime) {
                InfoL << "stop at " << index << ":" << ftime << " "<< path;
                break;
            }
        }
        else{
            InfoL << "skip " << index << " file " << path << " for parseTime";
        }
    }
    if (task->files.empty()) {
        task->Notify(-1, "no files");
        return ;
    }
    task->dstFile = prefix;

    InfoL << _info.stream << " makeSnap req " << maxSec << ",actually " << (now - ftime) << " s, with " << task->files.size() << " files";
    hv::EventLoopThreadPool::Instance()->loop()->async([task]{
        task->DoTask();
    });
}

void HlsMakerImp::onFlushLastSegment(uint64_t duration_ms) {
    //关闭并flush文件到磁盘
    _file = nullptr;
    _duration += duration_ms / 1000.0f;
    auto it = _segment_file_paths.find(current_index());
    if (it != _segment_file_paths.end()) {
        it->second.duration = duration_ms;
    }
    // InfoL << _info.stream << " onFlushLastSegment " << file_index() << ": " << duration_ms << "ms";
    GET_CONFIG(bool, broadcastRecordTs, Hls::kBroadcastRecordTs);
    if (broadcastRecordTs) {
        _info.time_len = duration_ms / 1000.0f;
        _info.file_size = File::fileSize(_info.file_path.data());
        NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastRecordTs, _info);
    }
}

std::shared_ptr<FILE> HlsMakerImp::makeFile(const string &file, bool setbuf) {
    auto file_buf = _file_buf;
    auto ret = shared_ptr<FILE>(File::create_file(file.data(), "wb"), [file_buf](FILE *fp) {
        if (fp) {
            fclose(fp);
        }
    });
    if (ret && setbuf) {
        setvbuf(ret.get(), _file_buf.get(), _IOFBF, _buf_size);
    }
    return ret;
}

void HlsMakerImp::setMediaSource(const string &vhost, const string &app, const string &stream_id) {
    _media_src = std::make_shared<HlsMediaSource>(vhost, app, stream_id);
    _info.app = app;
    _info.stream = stream_id;
    _info.vhost = vhost;
}

HlsMediaSource::Ptr HlsMakerImp::getMediaSource() const {
    return _media_src;
}

}//namespace mediakit