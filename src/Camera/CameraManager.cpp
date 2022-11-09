#include "WebRtcServer.h"
#include "Util/util.h"
#include "Util/File.h"
#include "Util/logger.h"
#include "Common/config.h"
#include "Common/MultiMediaSourceMuxer.h"
#include "Hls/HlsMediaSource.h"
#include "Hls/HlsRecorder.h"
#include "requests.h"
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
const string kSnapPort = "camera.port";
const string kSnapFreq = "camera.snapFreq";
const string kSnapType = "camera.snapType";
const string kSnapExpireDay = "camera.expireDay";
const string kApiToken = "camera.apiToken";
const string kApiUrl = "camera.apiUrl";
const string kUploadTimeout = "camera.uploadTimeout";
const string kLoadCameraInterval = "camera.loadCameraIntenval";
// 球馆id
const string kStadiumId = "camera.stadiumId";
// 球场id
const string kCourtId = "camera.courtId";

static onceToken token([]() {
    mINI::Instance()[kSnapSec] = 15;
    mINI::Instance()[kSnapExt] = "mp4";
    mINI::Instance()[kSnapType] = "Alarm Input";
    mINI::Instance()[kSnapPort] = 9701;
    mINI::Instance()[kSnapFreq] = 5;
    mINI::Instance()[kSnapExpireDay] = 7;
    mINI::Instance()[kApiToken] = "";
    mINI::Instance()[kApiUrl] = "";
    mINI::Instance()[kUploadTimeout] = 300;
    mINI::Instance()[kLoadCameraInterval] = 300;
    mINI::Instance()[kStadiumId] = "1";
    mINI::Instance()[kCourtId] = "";
});
GET_CONFIG(int, gSnapSec, kSnapSec);

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
    val["location"] = this->location;
    return true;
}

bool Camera::FromJson(hv::Json& val)
{
    stream = val["stream"].get<std::string>();
    url = val["url"].get<std::string>();
    record_url = val["record_url"].get<std::string>();
    desc = val["desc"].get<std::string>();
    location = val["location"].get<std::string>();
    return stream.length() && url.length();
}

std::string Camera::dump() const
{
    toolkit::_StrPrinter sp;
    sp << desc << "(" << stream << ") url:" << url << ", nvr:" << record_url << ", loc=" << location;
    return sp;
}

bool Camera::setId(const std::string& id)
{
    if (id.empty()) return false;
    auto vec = split(id, "/");
    switch (vec.size())
    {
    case 1:
        stream = std::string(DEFAULT_VHOST) + "/camera/" + vec[0];
        break;
    case 2:
        stream = std::string(DEFAULT_VHOST) + "/" + vec[0] + "/" + vec[1];
        break;
    default:
        stream = id;
        break;
    }
    return true;
}

bool Camera::startProxy() const
{
    if (url.empty()) return false;
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
    GET_CONFIG(std::string, gSnapExt, kSnapExt);
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

void Camera::makeSnap(time_t time, int snapSec, onSnap cb) {
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
    auto snapCb = [dstFile, tmpFile, cb, stream, time](time_t result, const string &err_msg) {
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
            std::string path = dstFile;
            CameraManager::Instance().setSnapPath(stream, time, path);
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
    path = snapUrl(path);
    if (path.length()) {
        InfoL << stream << " setSnapPath " << key << " path " << path;
        CameraSnap snap;
        snap.stream = stream;
        snap.time = key;
        snap.url = path;
        snap.update();

        auto it = _snaps_tasks.find(key);
        if (it != _snaps_tasks.end()) {
            it->second->FireCbs(key, path);
            _snaps_tasks.erase(it);
        }
    }
}

time_t Camera::addSnap(time_t time, int snapSec, onSnap cb)
{
    if (!cb) cb = [](time_t t, std::string msg) {
        InfoL << "addSnap " << t << " result " << msg;
    };

    bool current = false;
    if (!time) {
        time = ::time(nullptr);
        current = true;
        // 限频
        GET_CONFIG(int, gSnapFreq, kSnapFreq);
        if (last_snap && gSnapFreq > 0 && time - last_snap < gSnapFreq) {
            cb(0, "skip snap for freq limit");
            return 0;
        }
        last_snap = time;
    }

    CameraSnap snap;
    snap.time = time;
    snap.stream = stream;
    if (!snap.insert()) {
        cb(0, "db insert error");
        return 0;
    }

    if (current) {
        std::string snapPath = this->snapPath(time, snapSec);
        std::string key = stream;
        MediaInfo mi(stream);
        MediaSource::Ptr source = MediaSource::find(HLS_SCHEMA, mi._vhost, mi._app, mi._streamid);
        if (source) {
            // get record from memory
            HlsMediaSource::Ptr hls = std::dynamic_pointer_cast<HlsMediaSource>(source);
            HlsRecorder::Ptr record = std::dynamic_pointer_cast<HlsRecorder>(hls->getListener().lock());
            if (record && record->duration() >= snapSec) {
                File::create_path(snapPath.data(), 0x777);
                std::string tmpPath = snapPath + "_";
                record->makeSnap(snapSec, tmpPath, [cb, key, time, tmpPath, snapPath](bool result, std::string msg) {
                    if (result) {
                        File::delete_file(snapPath.data());
                        rename(tmpPath.data(), snapPath.data());
                        msg = snapPath;
                        CameraManager::Instance().setSnapPath(key, time, msg);
                    }
                    else {
                        File::delete_file(tmpPath.data());
                    }
                    cb(result?time:0, msg);
                });
                return time;
            }
        }
    }

    if (record_url.length()) {
        // nvr预览接口获取不到实时流，得加延迟才能调用, 但此时获取的又不是我们要的视频...
        // int nvrDelay = 120;
        makeSnap(time, snapSec);
        cb(time, "");
    }
    else {
        cb(0, "snap without nvr");
    }
    return time;
}

bool Camera::delSnap(time_t time)
{
    auto db = getDB();
    sqlite3pp::command cmd(*db, "delete from camera_snap where stream=? and time=?");
    cmd.binder() << stream << (long long int)time;
    bool ret = SQLITE_OK == cmd.execute();
    if (ret) {
        AutoLock l(_snap_lock);
        _snaps_tasks.erase(time);
    }
    //@todo delete files
    return ret;
}

void Camera::loadSnapTasks()
{
    AutoLock l(_snap_lock);
    _snaps_tasks.clear();
    time_t exp = 0;
    GET_CONFIG(int, gSnapExpireDay, kSnapExpireDay);
    if(gSnapExpireDay >0)
        exp = ::time(NULL) - 3600 * 24 * gSnapExpireDay;
    auto db = getDB();
    sqlite3pp::query qry(*db, "select stream, time from camera_snap where stream=? and time>? and url=''");
    qry.bind(1, stream, sqlite3pp::copy);
    qry.bind(2, (long long int)exp);
    for (auto it : qry) {
        CameraSnap snap;
        snap.stream = it.get<std::string>(0);
        snap.time = it.get<long long int>(1);
        makeSnap(snap.time, gSnapSec);
    }
    if (_snaps_tasks.size()) {
        InfoL << stream << " load " << _snaps_tasks.size() << " from db";
    }
}

int Camera::querySnaps(time_t begin, time_t end, std::list<CameraSnap>& snaps){
    snaps.clear();
    auto db = getDB();
    sqlite3pp::query qry(*db, "select stream, time, url from camera_snap where stream=? and time>? and time<?");
    qry.bind(1, stream, sqlite3pp::copy);
    qry.bind(2, (long long int)begin);
    qry.bind(3, (long long int)end);
    for (auto it : qry) {
        CameraSnap snap;
        snap.stream = it.get<std::string>(0);
        snap.time = it.get<long long int>(1);
        auto url = it.get<const char*>(2);
        if (url && url[0])
            snap.url = url;
        else
            snap.url.clear();
        snaps.push_back(snap);
    }
    InfoL << stream << " querySnaps " << begin << "->" << end << " return " << snaps.size() << " items";
    return snaps.size();
}

bool Camera::insert() const 
{
    auto db = getDB();
    sqlite3pp::command cmd(*db, "insert into camera(id, url, record_url, desc, location) VALUES (?,?,?,?,?)");
    cmd.binder() << this->stream << this->url << this->record_url << this->desc << this->location;
    int ret = cmd.execute();
    InfoL << "insert camera " << dump() << " return " << ret;
    return SQLITE_OK == ret;
}

bool Camera::update() const
{
    auto db = getDB();
    sqlite3pp::command update(*db, "update camera set url=?, record_url=?, desc=?, location=? where id=?");
    update.binder() << this->url << this->record_url << this->desc << location << this->stream;
    int ret = update.execute();
    InfoL << "update camera " << dump() << " return " << ret;
    return SQLITE_OK == ret;
}

////////////////////////////////////////////////////
// CameraManager
void CameraManager::createTable() {
    auto db = getDB();
    try {
        int dberr = db->execute("create table if not exists camera( \
            id      TEXT NOT NULL, \
            url	    TEXT NOT NULL, \
            record_url  TEXT, \
            desc    TEXT, \
            location TEXT, \
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
    }
    catch (std::exception& e) {
        WarnL << "sql exception:" << e.what();
        if (strstr(e.what(), "location")) {
            db->execute("alter table camera ADD location text");
        }
    }
}

void CameraManager::loadDbCamera()
{
    _cameras.clear();
    auto db = getDB();
    try {
        sqlite3pp::query qry(*db, "select id, url, record_url, desc, location from camera");
        for (auto it : qry) {
            Camera::Ptr cam = std::make_shared<Camera>();
            cam->stream = it.get<std::string>(0);
            cam->url = it.get<std::string>(1);
            cam->record_url = it.get<std::string>(2);
            cam->desc = it.get<std::string>(3);
            const char* loc = it.get<const char*>(4);
            if(loc && loc[0])
              cam->location = loc;
            InfoL << "loadCamera " << cam->dump();
            cam->loadSnapTasks();
            AddCamera(cam, false);
        }
    }
    catch (std::exception& e) {
        WarnL << "sql exception:" << e.what();
    }
}

void CameraManager::loadApiCamera()
{
    GET_CONFIG(std::string, gStadiumId, kStadiumId);
    GET_CONFIG(std::string, gCourtId, kCourtId);
    HttpRequestPtr req(new HttpRequest);
    req->SetMethod("GET");
    req->SetHeader("X-Auth-Token", gApiToken);
    req->url = gApiUrl + "/internal/cameras?stadiumId=" + gStadiumId;
    requests::async(req, [this](const HttpResponsePtr& resp) {
        if (!resp || resp->status_code != 200) {
            WarnL << "network err:" << (resp ? resp->content : "");
            return;
        }
        hv::Json root;
        if (!root.parse(resp->body)) {
            WarnL << "json parse error" << resp->body;
            return;
        }
        InfoL << "load " << root.size() << " Cameras from api";
        for (auto val : root)
        {
            if (!val.is_object())
                continue;
            Camera tmp;
            std::string id = val["id"].get<std::string>();
            if (!tmp.setId(id)) {
                WarnL << "skip camera " << val.dump();
                continue;
            }

            Camera::Ptr cam = GetCamera(tmp.stream);
            if (!cam) {
                InfoL << "got new camera " << val.dump();
                cam = std::make_shared<Camera>();
                cam->stream = tmp.stream;
                cam->loadSnapTasks();
            }
            cam->desc = val["name"].get<std::string>();
            cam->password = val["password"].get<std::string>();
            cam->location = val["courtId"].get<std::string>();
            cam->url = val["previewUrl"].get<std::string>();
            cam->record_url = val["nvrUrl"].get<std::string>();
            AddCamera(cam, false);
        }
    });
}

void CameraManager::setSnapPath(std::string stream, time_t time, std::string &path)
{
    auto camera = GetCamera(stream);
    if (!camera) return;
    if (gApiUrl.length()) {
        ///////////////////////////////http upload///////////////////////
        GET_CONFIG(int, gUploadTimeout, kUploadTimeout);
        HttpRequestPtr req(new HttpRequest);
        req->SetMethod("POST");
        req->url = gApiUrl + "/internal/videos";
        req->timeout = gUploadTimeout; // 10min
        req->content_type = MULTIPART_FORM_DATA;
        req->SetFormFile("file", path.c_str());
        //设置http请求头
        MediaInfo mi(stream);
        req->SetFormData("cameraId", mi._streamid);
        req->SetFormData("password", camera->password);
        req->SetFormData("shootAt", time);
        req->SetHeader("X-Auth-Token", gApiToken);
        //开启请求
        requests::async(req, [=](const HttpResponsePtr& resp) {
            if (!resp || resp->status_code != 200) {
                WarnL << "http request err:" << (resp ? resp->body : "");
                return;
            }
            InfoL << stream << " submit video " << (long)time << " return " << resp->body;
            std::string tmp = path;
            camera->setSnapPath(time, tmp);
        });
    }
    else {
        camera->setSnapPath(time, path);
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

    createTable();
    gApiUrl = mINI::Instance()[kApiUrl];
    gApiToken = mINI::Instance()[kApiToken];
    gSnapSec = mINI::Instance()[kSnapSec];
    if (gApiUrl.length()) {
        InfoL << "use api " << gApiUrl;
        _timer = std::make_shared<toolkit::Timer>(mINI::Instance()["kLoadCameraInterval"], 
            [this]() { loadApiCamera(); return true;},
            hv::EventLoopThreadPool::Instance()->nextLoop()
        );
    }
    else {
        loadDbCamera();
    }

    GET_CONFIG(int, gSnapPort, kSnapPort);
    if (gSnapPort > 0) {
      _tcp = std::make_shared<hv::TcpServer>();
      _tcp->onMessage = [](const hv::SocketChannelPtr&, hv::Buffer* buffer){
        
      };
      _tcp->createsocket(gSnapPort);
      _tcp->start();
    }

    GET_CONFIG(std::string, api_secret, "api.secret");
    auto http = getHttpService();
    http->Any("/index/api/getCameraList", [this](HttpRequest* req, HttpResponse* resp) -> int {
        CHECK_SECRET();
        bool refresh = req->GetBool("refresh");
        if (gApiUrl.length() && refresh) {
            loadApiCamera();
        }
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
        time_t start = time(NULL);
        auto arg = req->GetString("start");
        if (arg.length()) {
            start = std::stoi(arg);
        }
        time_t end = start + 24 * 3600;
        arg = arg = req->GetString("end");;
        if (arg.length()) {
            end = std::stoi(arg);
        }
        std::list<CameraSnap> snaps;
        cam->querySnaps(start, end, snaps);
        hv::Json val;
        for (auto it : snaps) {
            hv::Json snap;
            it.ToJson(snap);
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

        auto cb = [writer](time_t result, std::string msg) {
            hv::Json val;
            if (result) {
                val["result"] = true;
                val["code"] = API::Success;
                val["msg"] = "success";
                val["data"] = msg;
                val["time"] = (uint64_t)result;
            }
            else{
                val["result"] = false;
                val["code"] = API::OtherFailed;
                val["msg"] = msg;
            }
			writer->End(val.dump());
        };

        cam->addSnap(0, gSnapSec, cb);
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
        /*
        if (!cam->hasSnap(time)) {
            bool add = req->GetBool("add");
            if (!add) {
				throw SockException(Err_other, "no snap");
            }
        }
        */

        auto cb = [writer, key, time](time_t result, std::string msg) {
            hv::Json val;
            if (result) {
                val["result"] = true;
                val["code"] = API::Success;
                val["msg"] = "success";
                val["data"] = msg;
                val["time"] = (int64_t)time;
            }
            else {
                val["result"] = false;
                val["code"] = API::OtherFailed;
                val["msg"] = msg;
            }
			writer->End(val.dump());
        };

        int snapSec = gSnapSec;
        auto dur = req->GetString("duration");
        if (dur.length()) {
            snapSec = std::stoi(dur);
        }
        cam->addSnap(time, snapSec, cb);
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
        cam->setId(req->GetString("stream"));
		cam->url = req->GetString("url");
        cam->record_url = req->GetString("record_url");
        cam->desc = req->GetString("desc");
        cam->location = req->GetString("location");

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
    _tcp = nullptr;
    if (hook_tag) {
        NoticeCenter::Instance().delListener(&hook_tag);
        hook_tag = nullptr;
    }
}

Camera::Ptr CameraManager::findCamera(const std::string& name)
{
  Camera::Ptr ret;
  AutoLock l(_mutex);
  for (auto it : _cameras) {
    if (it.second->desc == name) {
      ret = it.second;
      break;
    }
  }
  return ret;
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