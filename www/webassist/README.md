# zlm_webassist


[![](https://img.shields.io/badge/license-MIT-green.svg)](https://github.com/1002victor/zlm_webassist/blob/main/LICENSE)
![](https://img.shields.io/badge/language-html-red.svg)
![](https://img.shields.io/badge/language-vue-green.svg)
![](https://img.shields.io/badge/language-js-black.svg)
![](https://img.shields.io/badge/language-css-yelllow.svg)
[![](https://img.shields.io/badge/platform-linux%20|%20macos%20|%20windows-blue.svg)](https://github.com/ZLMediaKit/ZLMediaKit)
[![](https://img.shields.io/badge/PRs-welcome-yellow.svg)](https://github.com/1002victor/zlm_webassist/pulls)


简体中文 | [English](./README_EN.md)


## 项目介绍

本项目是[ZLMediakit](https://github.com/ZLMediaKit/ZLMediaKit)的 web 管理助手,更改参数，添加代理，踢客户端这三个常用功能作为ZLM的调试助手非常方便，方便日常调试管理使用。

仅有前端代码，无后端。 而且代码超级简单，路过的前端大佬见笑了。

不建议部署在生产环境对外提供服务使用!!!

不建议部署在生产环境对外提供服务使用!!!

不建议部署在生产环境对外提供服务使用!!!

## 功能清单：

目前实现的功能：

- [x] 统计线程情况

- [x] 获取流列表

- [x] 关闭流

- [x] 获取 Session 列表

- [x] 关闭 Session

- [x] webrt 测试（在 [ZLMediakit](https://github.com/ZLMediaKit/ZLMediaKit/blob/master/www/webrtc/index.html) 的基础上仅做了页面优化，以及基于 vue2 的代码重构）

- [x] 修改 server 配置

- [x] 推流代理管理(添加，删除和列表)

- [x] 拉流代理管理(添加，删除和列表)

- [x] FFmpeg推拉流管理(添加，删除和列表)

- [x] RTP服务管理(添加，删除，连接远端、设置ssrc和列表)

- [x] 获取 某个 MediaSource 的 Player 列表

- [x] 重启服务器

待实现功能：

- [ ] 开启、关闭录制

- [ ] 查看录制视频

- [ ] 点播播放

- [ ] 直播播放


## 如何使用

直接放在zlm的www根目录下即可。

访问的时候务必在url中加上secret参数，否则就是个花架子，看不到数据。

如果打开网页是乱码，修改config.ini配置文件中http的charSet为utf-8，然后再重启MediaServer

## 附录

### 目前用到的zlm的接口


- [x] "/index/api/addFFmpegSource",
- [x] "/index/api/addStreamProxy",
- [x] "/index/api/close_stream",
- [x] "/index/api/close_streams",
- [x] "/index/api/delFFmpegSource",
- [x] "/index/api/delStreamProxy",
- [x] "/index/api/getAllSession",
- [x] "/index/api/getApiList",
- [x] "/index/api/getMediaList",
- [x] "/index/api/getServerConfig",
- [x] "/index/api/getThreadsLoad",
- [x] "/index/api/getWorkThreadsLoad",
- [x] "/index/api/kick_session",
- [x] "/index/api/kick_sessions",
- [x] "/index/api/restartServer",
- [x] "/index/api/setServerConfig",
- [ ] "/index/api/isMediaOnline",
- [ ] "/index/api/getMediaInfo",
- [ ] "/index/api/getRtpInfo",
- [ ] "/index/api/getMp4RecordFile",
- [ ] "/index/api/startRecord",
- [ ] "/index/api/stopRecord",
- [ ] "/index/api/getRecordStatus",
- [ ] "/index/api/getSnap",
- [x] "/index/api/updateRtpServerSSRC",
- [x] "/index/api/connectRtpServer",
- [x] "/index/api/openRtpServer",
- [x] "/index/api/closeRtpServer",
- [x] "/index/api/listRtpServer",
- [x] "/index/api/listStreamPusherProxy",
- [x] "/index/api/listStreamProxy",
- [x] "/index/api/listFFmpegSource",
- [ ] "/index/api/startSendRtp",
- [ ] "/index/api/stopSendRtp",
- [x] "/index/api/getStatistic",
- [x] "/index/api/addStreamPusherProxy",
- [x] "/index/api/delStreamPusherProxy",
- [x] "/index/api/version",
- [x] "/index/api/getMediaPlayerList"
