#ifndef MEDIA_SERVER_CAMERA_MANAGER_H
#define MEDIA_SERVER_CAMERA_MANAGER_H
#pragma once
#include <map>
#include <mutex>
#include <string>
#include "json.hpp"
#include "toolkit.h"
#include "NvrRecord.h"

struct CameraSnap {
    std::string stream;
    // 开始时间
    time_t time;
    // 存储路径
    std::string url;

    bool valid() const {
        return time > 0 && !stream.empty();
    }
    bool ToJson(nlohmann::json& val) const;
    bool FromJson(nlohmann::json& val);

    // db 操作
    bool update() const;
    bool insert() const;
};

struct Camera {
    typedef std::shared_ptr<Camera> Ptr;
    // 发布流id
    std::string stream;
    /*
     实时流url, 格式如下：
     海康URL: rtsp://username:password@<address>:<port>/Streaming/Channels/<id>
     大华URL: rtsp://username:password@ip:port/cam/realmonitor?channel=1&subtype=0
       rtsp://admin:abcde12346@192.168.1.150/cam/realmonitor?channel=1&subtype=1
     */
    std::string url;
    /*
     回放流url, 格式如下：
     海康URL: rtsp://username:password@ip:port/Streaming/tracks/1701?starttime=20131013t093812z&endtime=20131013t104816z
       rtsp://admin:12345@172.6.22.106:554/Streaming/tracks/1701?starttime=%Y%m%dt%H%M%Sz
     大华URL: rtsp://username:password@ip:port/cam/playback?channel=1&subtype=0&starttime=YYYY_MM_DD_HH_mm_SS&endtime=YYYY_MM_DD_HH_mm_SS
       rtsp://admin:abcde12346@192.168.1.150/cam/playback?channel=1&subtype=0&starttime=%Y_%m_%d_%H_%M_%S
     
     */
    std::string record_url;
    // 摄像头描述
    std::string desc;

    bool ToJson(nlohmann::json& val) const;
    bool FromJson(nlohmann::json& val);
    std::string dump() const;

    bool startProxy() const;
    bool stopProxy() const;

    bool update() const;
    bool insert() const;

    time_t addSnap(time_t time);
    bool delSnap(time_t time);
    bool hasSnap(time_t key) const { return _snaps.count(key); }
    void loadSnaps();

    std::string snapPath(time_t time, int sec) const;
    std::string snapUrl(std::string path) const;
    void setSnapPath(time_t key, std::string &path);
    // 从nvr中请求录像
    void makeSnap(
        time_t time,   ///< 时间
        int duration, ///< 时长
        std::function<void (bool, std::string)> cb = nullptr ///< 完成回调
    );

    // 快照列表
    std::map<time_t, CameraSnap> _snaps;
protected:
    // 快照拉去任务列表
    std::map<time_t, std::shared_ptr<mediakit::NvrRecord>> _snaps_tasks;
    // 快照锁
    std::recursive_mutex _snap_lock;
    // 用于限频addSnap
    time_t last_snap = 0;
};

class CameraManager
{
    void* hook_tag = nullptr;
    std::recursive_mutex _mutex;
    std::map<std::string, Camera::Ptr> _cameras;
    std::shared_ptr<toolkit::Timer> _timer;
    void loadSnaps();
public:
    static CameraManager &Instance();
    CameraManager() = default;

    void Init();
    void Destroy();

    void loadCameras();

    Camera::Ptr GetCamera(const std::string& key);
    bool AddCamera(Camera::Ptr cam, bool saveDB = true);
    bool DelCamera(const std::string& key);
};
#endif

