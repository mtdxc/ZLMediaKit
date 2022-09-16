#include "WebRtcServer.h"
#include "Util/util.h"
#include "Util/File.h"
#include "Util/logger.h"
#include "Common/config.h"
#include "Common/MultiMediaSourceMuxer.h"
#include "Hls/HlsMediaSource.h"
#include "Hls/HlsRecorder.h"
#include "CameraManager.h"
#include "sqlite3/sqlite3pp.h"

using std::string;
using namespace toolkit;
using namespace mediakit;
using AutoLock = std::unique_lock<std::recursive_mutex>;

INSTANCE_IMP(CameraManager);

// 缺省截屏时长
const string kSnapSec = "camera.snapSec";
const string kSnapExt = "camera.extension";
static onceToken token([]() {
    mINI::Instance()[kSnapSec] = 15;
    mINI::Instance()[kSnapExt] = "mp4";
});
GET_CONFIG(int, gSnapSec, kSnapSec);
GET_CONFIG(std::string, gSnapExt, kSnapExt);

std::shared_ptr<sqlite3pp::database> getDB() {
    static std::string dbPath = exeDir() + "zlm.db";
    static sqlite3pp::database db(dbPath.c_str());
    static std::recursive_mutex mutex;
    mutex.lock();
    std::shared_ptr<sqlite3pp::database> ret(&db, [](sqlite3pp::database *) { mutex.unlock(); });
    return ret;
}

////////////////////////////////////////////////////
// Camra
bool Camera::ToJson(hv::Json& val) const
{
    val["stream"] = this->stream;
    val["url"] = this->url;
    val["record_url"] = this->record_url;
    val["desc"] = this->desc;
    return true;
}

bool Camera::FromJson(hv::Json& val)
{
    stream = val["stream"].get<std::string>();
    url = val["url"].get<std::string>();
    record_url = val["record_url"].get<std::string>();
    desc = val["desc"].get<std::string>();
    return stream.length() && url.length();
}

std::string Camera::dump() const
{
    toolkit::_StrPrinter sp;
    sp << desc << "(" << stream << ") url:" << url << ", nvr:" << record_url;
    return sp;
}

bool Camera::startProxy() const
{
    static auto cb = [](const SockException &ex, const string &key) {};
    MediaInfo info(stream);
    mediakit::ProtocolOption option;
    // 利用hls来保存最近30s录像
    option.enable_hls = true;
    addStreamProxy(info._vhost, info._app, info._streamid, this->url, 0, option, 0, 10, cb);
    return true;
}

bool Camera::stopProxy() const
{
    return delStreamProxy(stream);
}

std::string Camera::snapPath(time_t t, int sec) const {
    auto date = getTimeStr("%Y-%m-%d", t);
    auto time = getTimeStr("%H-%M-%S", t);
    MediaInfo mi(stream);
    std::string recordDir = Recorder::getRecordPath(Recorder::type_mp4, mi._vhost, mi._app, mi._streamid);
    return recordDir + date + "/" + time + "_" + std::to_string(sec) + "." + gSnapExt;
}

std::string Camera::snapUrl(std::string path) const
{
    // 绝对路径转相对路径
    GET_CONFIG(string, recordPath, Protocol::kMP4SavePath);
    std::string dir = File::absolutePath("", recordPath);
    if (start_with(path, dir)) {
        path = path.substr(dir.length());
    }
    return path;
}

void Camera::makeSnap(time_t time, int snapSec, std::function<void (bool, std::string)> cb) {
    std::string path = snapPath(time, snapSec);
    std::string dstFile = path;
    if (File::fileExist(dstFile.c_str())) {
        if(cb)
            cb(true, dstFile);
        return;
    }
    if (record_url.empty())
        return;

    AutoLock l(_snap_lock);
    auto it = _snaps_tasks.find(time);
    if (it!=_snaps_tasks.end()) {
        if(cb)
            it->second->addCbs(cb);
        return;
    }

    File::create_path(path.c_str(), 0x777);
    std::string url = getTimeStr(record_url.c_str(), time - snapSec);
    std::string tmpFile = path + "_";
    //InfoL << stream << " path " << dstFile << " from url " << url;
    std::string stream = this->stream;
    auto snapCb = [dstFile, tmpFile, cb, stream, time](bool result, const string &err_msg) {
        if (!result) {
            //生成截图失败，可能残留空文件
            File::delete_file(tmpFile.data());
            cb(result, err_msg);
        }
        else {
            //临时文件改成正式文件
            if (dstFile != tmpFile) {
                File::delete_file(dstFile.data());
                rename(tmpFile.data(), dstFile.data());
            }
            if (auto cam = CameraManager::Instance().GetCamera(stream)) {
                std::string path = dstFile;
                cam->setSnapPath(time, path);
            }
        }
    };
    //FFmpegSnap::makeRecord(url, tmpFile, snapSec, snapCb);
    auto task = std::make_shared<NvrRecord>(url, tmpFile, snapSec, snapCb);
    if (cb)
        task->addCbs(cb);
    _snaps_tasks[time] = task;
    task->Connect();
}

void Camera::setSnapPath(time_t key, std::string& path)
{
    AutoLock l(_snap_lock);
    if (_snaps.count(key)) {
        MediaInfo mi(stream);
        path = snapUrl(path);
        if (path.length() && _snaps[key].url != path) {
            InfoL << stream << " setSnapPath " << key << " path " << path;
            _snaps[key].url = path;
            _snaps[key].update();
        }
        if (path.length()) {
            auto it = _snaps_tasks.find(key);
            if (it != _snaps_tasks.end()) {
                it->second->FireCbs(true, path);
                _snaps_tasks.erase(it);
            }
        }
    }
}

time_t Camera::addSnap(time_t time)
{
    if (!time) {
        time = ::time(nullptr);
        // 限频
        if (last_snap && time - last_snap < 5) {
            InfoL << "skip snap for freq limit";
            return 0;
        }
        last_snap = time;
    }

    CameraSnap snap;
    snap.time = time;
    snap.stream = stream;
    if (snap.insert()) {
        AutoLock l(_snap_lock);
        _snaps[time] = snap;
    }
    else {
        time = 0;
    }
    return time;
}

bool Camera::delSnap(time_t time)
{
    bool ret = false;
    {
        AutoLock l(_snap_lock);
        ret = _snaps.erase(time);
        _snaps_tasks.erase(time);
    }
    if (ret) {
        auto db = getDB();
        sqlite3pp::command cmd(*db, "delete from camera_snap where stream=? and time=?");
        cmd.binder() << stream << (long long int)time;
        ret = SQLITE_OK == cmd.execute();
        //@todo delete files
    }
    return ret;
}

void Camera::loadSnaps()
{
    AutoLock l(_snap_lock);
    _snaps.clear();
    _snaps_tasks.clear();
    time_t exp = ::time(NULL) + 30;
    auto db = getDB();
    sqlite3pp::query qry(*db, "select stream, time, url from camera_snap where stream=?");
    qry.bind(1, stream, sqlite3pp::copy);
    
    for (auto it : qry) {
        CameraSnap snap;
        snap.stream = it.get<std::string>(0);
        snap.time = it.get<long long int>(1);
        auto url = it.get<const char*>(2);
        if (url && url[0])
            snap.url = url;
        else
            snap.url.clear();
        _snaps[snap.time] = snap;
        // if (snap.url.empty())
            makeSnap(snap.time, gSnapSec);
    }
    if (_snaps.size()) {
        InfoL << stream << " load " << _snaps.size() << " from db, " << _snaps_tasks.size() << " unloaded";
    }
}

bool Camera::insert() const 
{
    auto db = getDB();
    sqlite3pp::command cmd(*db, "insert into camera(id, url, record_url, desc) VALUES (?,?,?,?)");
    cmd.binder() << this->stream << this->url << this->record_url << this->desc;
    int ret = cmd.execute();
    InfoL << "insert camera " << dump() << " return " << ret;
    return SQLITE_OK == ret;
}

bool Camera::update() const
{
    auto db = getDB();
    sqlite3pp::command update(*db, "update camera set url=?, record_url=?, desc=? where id=?");
    update.binder() << this->url << this->record_url << this->desc << this->stream;
    int ret = update.execute();
    InfoL << "update camera " << dump() << " return " << ret;
    return SQLITE_OK == ret;
}

////////////////////////////////////////////////////
// CameraManager
void CameraManager::loadCameras()
{
    _cameras.clear();
    try {
        auto db = getDB();
        int dberr = db->execute("create table if not exists camera( \
            id      TEXT NOT NULL, \
            url	    TEXT NOT NULL, \
            record_url  TEXT, \
            desc    TEXT, \
            PRIMARY KEY(id))");
        if (dberr != SQLITE_OK) {
            WarnL << "create table camera error: " << db->error_msg();
            return;
        }

        dberr = db->execute("create table if not exists camera_snap( \
            stream	TEXT, \
            time	INTEGER, \
            duration	INTEGER, \
            url	TEXT, \
            PRIMARY KEY(stream, time))");
        if (dberr != SQLITE_OK) {
            WarnL << "create table camera_snap error: " << db->error_msg();
            return;
        }

        sqlite3pp::query qry(*db, "select id, url, record_url, desc from camera");
        for (auto it : qry) {
            Camera::Ptr cam = std::make_shared<Camera>();
            cam->stream = it.get<std::string>(0);
            cam->url = it.get<std::string>(1);
            cam->record_url = it.get<std::string>(2);
            cam->desc = it.get<std::string>(3);
            InfoL << "loadCamera " << cam->dump();
            cam->loadSnaps();
            AddCamera(cam, false);
        }
    }
    catch (std::exception& e) {
        WarnL << "loadCameras sql exception:" << e.what();
    }
}

#define CHECK_SECRET() \
    if(req->client_addr.ip != "127.0.0.1"){ \
        if(api_secret != req->GetString("secret")){ \
            throw SockException(Err_refused, "secret错误"); \
        } \
    }
#define CHECK_ARGS(...)  
enum API{
	Success,
	OtherFailed = -1
};
void CameraManager::Init() {
    mediakit::HlsRecorder::setMaxCache(gSnapSec);

    loadCameras();

    GET_CONFIG(std::string, api_secret, "api.secret");
    auto http = getHttpService();
    http->Any("/index/api/getCamaraList", [this](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        //获取所有Camear列表
        hv::Json val;
        for (auto it : _cameras) {
            auto& cam = it.second;
            hv::Json item;
            if (cam->ToJson(item)) {
                val["data"].push_back(item);
            }
        }
        return resp->Json(val);
    });

	http->Any("/index/api/getCameraInfo", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("key");
        auto cam = CameraManager::Instance().GetCamera(req->GetString("key"));
        if (!cam) {
            throw SockException(Err_other, "can not find the camera");
        }
		hv::Json val;
        cam->ToJson(val["data"]);
		return resp->Json(val);
    });

	http->Any("/index/api/delCamera", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("key");
        bool result = CameraManager::Instance().DelCamera(req->GetString("key"));
		hv::Json val;
        val["result"] = result;
        val["code"] = result ? API::Success : API::OtherFailed;
        val["msg"] = result ? "success" : "delCamera failed";
		return resp->Json(val);
    });

	http->Any("/index/api/getCameraDumps", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("key");
        auto cam = CameraManager::Instance().GetCamera(req->GetString("key"));
        if (!cam) {
            throw SockException(Err_other, "can not find the camera");
        }
		hv::Json val;
        for (auto it : cam->_snaps) {
            hv::Json snap;
            it.second.ToJson(snap);
            val["data"].push_back(snap);
        }
		return resp->Json(val);
    });

    // 根据当前时间增加一个cameraSnap
	http->Any("/index/api/dumpCamera", [](const HttpContextPtr& ctx) -> int {
		auto req = ctx->request;
		auto writer = ctx->writer;
		CHECK_SECRET();
        CHECK_ARGS("key");
        std::string key = req->GetString("key");
        auto cam = CameraManager::Instance().GetCamera(key);
        if (!cam) {
			throw SockException(Err_other, "can not find the camera");
		}

        time_t time = cam->addSnap(0);
        if (!time) {
			throw SockException(Err_other, "camera addSnap error");
        }

        auto cb = [writer, time](bool result, std::string msg) {
            hv::Json val;
            val["result"] = result;
            if(result){
                val["code"] = API::Success;
                val["msg"] = "success";
                val["data"] = msg;
                val["time"] = (uint64_t)time;
            }
            else{
                val["code"] = API::OtherFailed;
                val["msg"] = msg;
            }
			writer->End(val.dump());
        };

        int snapSec = gSnapSec;
        std::string snapPath = cam->snapPath(time, snapSec);
        MediaInfo mi(cam->stream);
        MediaSource::Ptr source = MediaSource::find(HLS_SCHEMA, mi._vhost, mi._app, mi._streamid);
        if (source) {
            // get record from memory
            HlsMediaSource::Ptr hls = std::dynamic_pointer_cast<HlsMediaSource>(source);
            HlsRecorder::Ptr record = std::dynamic_pointer_cast<HlsRecorder>(hls->getListener().lock());
            if (record && record->duration() >= snapSec) {
                File::create_path(snapPath.data(), 0x777);
                std::string tmpPath = snapPath + "_";
                record->makeSnap(snapSec, tmpPath, [cb, key, time, tmpPath, snapPath] (bool result, std::string msg) {
                    if (result) {
                        File::delete_file(snapPath.data());
                        rename(tmpPath.data(), snapPath.data());
                        msg = snapPath;
                        auto cam = CameraManager::Instance().GetCamera(key);
                        if(cam)
                            cam->setSnapPath(time, msg);
                    }
                    else {
                        File::delete_file(tmpPath.data());
                    }
                    cb(result, msg);
                });
                return 0;
            }
        }

        if (cam->record_url.length()) {
            // nvr预览接口获取不到实时流，得加延迟才能调用, 但此时获取的又不是我们要的视频...
            // int nvrDelay = 120;
            cam->makeSnap(time, snapSec);
            cb(true, "");
        }
        else{
            cb(false, "unsupported method");
        }
		return 0;
    });

	http->Any("/index/api/dumpCameraSnap", [](const HttpContextPtr& ctx) -> int {
		auto req = ctx->request;
		auto writer = ctx->writer;
        CHECK_SECRET();
        CHECK_ARGS("key", "time");
        std::string key = req->GetString("key");
        auto cam = CameraManager::Instance().GetCamera(key);
        if (!cam) {
			throw SockException(Err_other, "can not find the camera");
		}

        time_t time = req->GetInt("time");
        if (!cam->hasSnap(time)) {
            bool add = req->GetBool("add");
            if (!add) {
				throw SockException(Err_other, "no snap");
            }
            time = cam->addSnap(time);
            if(!time)
				throw SockException(Err_other, "add snap error");
        }

        auto cb = [writer, key, time](bool result, std::string msg) {
            hv::Json val;
            val["result"] = result;
            if (result) {
                val["code"] = API::Success;
                val["msg"] = "success";
                val["data"] = msg;
                val["time"] = (int64_t)time;
            }
            else {
                val["code"] = API::OtherFailed;
                val["msg"] = msg;
            }
			writer->End(val.dump());
        };

        if (cam->record_url.empty()) {
            InfoL << key << " dumpCameraSnap without nvr ***";
            cb(true, "");
            return 0;
        }

        int snapSec = gSnapSec;
        auto dur = req->GetString("duration");
        if (dur.length()) {
            snapSec = std::stoi(dur);
        }
        cam->makeSnap(time, snapSec, cb);
		return 0;
        return 0;
    });

	http->Any("/index/api/delCameraSnap", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("key", "time");
        std::string key = req->GetString("key");
        auto cam = CameraManager::Instance().GetCamera(key);
        if (!cam) {
            throw SockException(Err_other, "can not find the camera");
        }

        time_t time = req->GetInt("time");
        bool result = cam->delSnap(time);
		hv::Json val;
        val["result"] = result;
        val["code"] = result ? API::Success : API::OtherFailed;
        val["msg"] = result ? "success" : "delCameraSnap failed";
		return resp->Json(val);
    });

    //测试url http://127.0.0.1/index/api/getMediaInfo?schema=rtsp&vhost=__defaultVhost__&app=live&stream=obs
	http->Any("/index/api/addCamera", [](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        CHECK_ARGS("stream", "url");
        Camera::Ptr cam = std::make_shared<Camera>();
        cam->stream = req->GetString("stream");
        if (cam->stream.length() && cam->stream[0] == '/')
            cam->stream = DEFAULT_VHOST + cam->stream;
		cam->url = req->GetString("url");
        cam->record_url = req->GetString("record_url");
        cam->desc = req->GetString("desc");

        bool ret = CameraManager::Instance().AddCamera(cam);
		hv::Json val;
        val["result"] = ret;
        if (ret) {
            cam->ToJson(val["data"]);
            val["key"] = cam->stream;
            val["code"] = API::Success;
        }
        else {
            val["code"] = API::OtherFailed;
            val["msg"] = "addCamera error";
        }
		return resp->Json(val);
    });

    //监听播放失败(未找到特定的流)事件
    NoticeCenter::Instance().addListener(&hook_tag, Broadcast::kBroadcastNotFoundStream, [](BroadcastNotFoundStreamArgs) {
        auto cam = CameraManager::Instance().GetCamera(args.shortUrl());
        if (cam) {
            cam->startProxy();
        }
    });
    // hook无人观看自动关流
    NoticeCenter::Instance().addListener(&hook_tag,Broadcast::kBroadcastStreamNoneReader,[](BroadcastStreamNoneReaderArgs) {
        std::string key = StrPrinter << sender.getVhost() << "/" << sender.getApp() << "/" << sender.getId();
        auto cam = CameraManager::Instance().GetCamera(key);
        if (cam && cam->record_url.length()) {
            //带nvr回放的摄像头，无人观看时立即停止拉流
            sender.close(false);
            WarnL << "无人观看主动关闭流:" << sender.getOriginUrl();
            return;
        }
    });
}

void CameraManager::Destroy()
{
    _timer = nullptr;
    if (hook_tag) {
        NoticeCenter::Instance().delListener(&hook_tag);
        hook_tag = nullptr;
    }
}

Camera::Ptr CameraManager::GetCamera(const std::string& key) {
    Camera::Ptr ret;
    AutoLock l(_mutex);
    auto it = _cameras.find(key);
    if (it!=_cameras.end()){
        ret = it->second;
    }
    return ret;
}

bool CameraManager::AddCamera(Camera::Ptr cam, bool save) {
    try {
        AutoLock l(_mutex);
        auto it = _cameras.find(cam->stream);
        if (it != _cameras.end()) {
            if (it->second && cam->url != it->second->url) {
                InfoL << cam->stream << " url change " << it->second->url << "->" << cam->url << ",call delStreamProxy";
                delStreamProxy(it->first);
            }
            if (save) cam->update();
        }
        else {
            if (save) cam->insert();
        }
        _cameras[cam->stream] = cam;
    }
    catch (std::exception& e) {
        WarnL << "AddCamera exception:" << e.what();
        return false;
    }
    // 当摄像头没配置nvr时，为了能第一时间得到录像，得立马启动拉流
    if (cam->record_url.empty()) {
        cam->startProxy();
    }
    return true;
}

bool CameraManager::DelCamera(const std::string& key) {
    {
        AutoLock l(_mutex);
        auto it = _cameras.find(key);
        if (it == _cameras.end()) {
            return false;
        }
        _cameras.erase(it);
    }

    InfoL << "delCamera " << key;
    delStreamProxy(key);

    try {
        auto db = getDB();
        sqlite3pp::command cmd(*db, "delete from camera where id = ?");
        cmd.bind(1, key, sqlite3pp::nocopy);
        cmd.execute();
        /* 一起删除snap列表
        sqlite3pp::command cmd2(*db, "delete from camera_snap where media = ?");
        cmd.bind(1, key, sqlite3pp::nocopy);
        cmd.execute();
        */
    } catch(std::exception& e) {
        WarnL << "sql exception:" << e.what();
    }
    return true;
}

void CameraManager::loadSnaps() {
    auto db = getDB();
    // sql加载未截屏的录像
    sqlite3pp::query qry(*db, "select stream, time, url from camera_snap \
        where url is null and media in (select id from camera where record_url is not null)");
    for (auto it : qry) {
        CameraSnap snap;
        snap.stream = it.get<std::string>(0);
        snap.time = it.get<long long int>(1);
        snap.url = it.get<std::string>(2);
    }
}

////////////////////////////////////////////////////
// CameraSnap
bool CameraSnap::ToJson(hv::Json& val) const
{
    val["stream"] = stream;
    val["time"] = (int64_t)time;
    val["url"] = url;
    return true;
}

bool CameraSnap::FromJson(hv::Json& val)
{
    stream = val["stream"].get<std::string>();
    time = val["time"].get<int64_t>();
    url = val["url"].get<std::string>();
    return valid();
}

bool CameraSnap::insert() const
{
    auto db = getDB();
    sqlite3pp::command cmd(*db, "insert into camera_snap(stream, time) VALUES (?,?)");
    cmd.binder() << stream << (long long int)time;
    int ret = cmd.execute();
    InfoL << "insert camera_snap " << stream << ", " << time  << " return " << ret;
    return SQLITE_OK == ret;
}

bool CameraSnap::update() const
{
    auto db = getDB();
    sqlite3pp::command cmd(*db, "update camera_snap set url=? where stream=? and time=?");
    cmd.binder() << url << stream << (long long int)time;
    int ret = cmd.execute();
    InfoL << "update camera_snap " << stream << ", " << time << ", url=" << url << " return " << ret;
    return SQLITE_OK == ret;
}