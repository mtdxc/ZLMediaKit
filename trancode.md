# 使用场景
- webrtc音频转码: 标准webrtc不支持AAC音频，而标准RTMP也只支持AAC音频，当RTMP推流遇到webrtc拉流时，会听不到声音；
- RTMP音频转码: 标准RTMP默认只支持AAC格式音频和H264视频，当收到zlm支持的其他媒体格式时，也会导致没声音或视频；
- 高清视频的接收：假如用户推了一路1080p的视频流(rtmp://vhost/app/stream)，播放器接收和解码这路流是需要一定的带宽和CPU的，
当机器性能不够时，可采用订阅rtmp://vhost/app/stream_720方式，来获取720p的小视频流，服务器会在必要时(StreamNotFound)启动转码，并在不需要时(StreamNoReader)停止转码；
- HLS自适应流传输：前面的机制需要手工实现客户端来进行码流切换，但HLS有个hls adaptive streaming技术，可做到自动切换，
注意HLS自适应流地址为 http://vhost/app/stream.m3u8, 而非旧的 http://vhost/app/stream/hls.m3u8，旧地址只能获取到某一特定分辨率的流

# 相对于ffmpeg推拉流转码的优缺点
之前zlm推荐的转码场景是通过启动ffmpeg进程进行拉流，后经转码后，并重新推向zlm中，这有个天然的好处是
- 可利用多台机器资源来实现并发转码
- 可应用FFmpeg中各种各样的滤镜
这是本方案所不及的；但进程内转码的方案也有如下优点：
- 省去启动多个进程方式，能节约点资源占用
- 可与MediaServer实现更紧密和结合，如实现无人观看不转码等功能；
- 能通过开发实现复杂的二次开发，这些功能可能无法很简单地通过FFmpeg命令行来实现；
当然，转码分支，也仍支持原有的推拉实现，用户可根据自身场景选择最合适的实现；

# 如何编译
## FFMPEG
转码底层使用FFMPEG来实现，需要打开FFMPEG, 即编译时必须指定 -DENABLE_FFMPEG=1,  当前已知支持FFMPEG 4.x 5.x 和 6.0，
 在ubuntu中可通过以下指令来安装: 
 ```
 apt-get install libavcodec-dev libavutil-dev libswscale-dev libresample-dev
 ```
 由于 ffmpeg 内置的opus编码器帧大小比较小2ms，建议自己编译ffmpeg时打开libopus集成

## WEBRTC可选
此外转码分支最早用于解决webrtc播放AAC音频没声音的问题，因此一般也会同时开启WEBRTC功能, 即-DENABLE_WEBRTC=1, 
此时必须先装好libsrtp库, 安装过程详见[wiki](https://github.com/ZLMediaKit/ZLMediaKit/wiki/zlm%E5%90%AF%E7%94%A8webrtc%E7%BC%96%E8%AF%91%E6%8C%87%E5%8D%97)

# 配置开关
- 音频转码项可通过audio_transcode配置项来配置，或是hook来打开，默认打开
- 宽度转码功能可通过transcode_size来配置，默认打开
- hls自适应流，通过indexCount和baseWidth来配置

```
[protocol]
# 开启音频自动转码功能
audio_transcode=1

[general]
# 转码成opus音频时的比特率
opusBitrate=64000
# 转码成AAC音频时的比特率
aacBitrate=64000
# 开启指定宽度转码功能
transcode_size=1

[hls]
# HLS自适应流配置，由于此功能是基于视频转码来实现的，因此也必须打开此功能前，也必须设置transcode_size=1
# 索引文件个数
# 之前HLS URL逻辑为 http://vhost/app/stream/hls.m3u8;
# 当kIndexCount>0后，会在http://vhost/app/stream.m3u8下生成m3u8的索引文件，用于多流切换
# 具体生成多少个流，取决于baseWidth和indexCount
indexCount=2
# 基础宽度, 大于此宽度的Hls流会生成indexCount个hls子流
baseWidth=640

```
注意如果编译时没启用FFMPEG，这些选项会自动关闭，使用此分支前得先确保启用FFMPEG！
