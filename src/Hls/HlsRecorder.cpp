#include "HlsRecorder.h"
#include "HlsMediaSource.h"
#include "Common/config.h"

namespace mediakit {
void HlsRecorder::onWrite(std::shared_ptr<toolkit::Buffer> buffer, uint64_t timestamp, bool key_pos)
{
    if (!buffer) {
        _hls->inputData(nullptr, 0, timestamp, key_pos);
    }
    else {
        _hls->inputData(buffer->data(), buffer->size(), timestamp, key_pos);
    }
}

void HlsRecorder::setMaxCache(int maxSec)
{
    auto& ini = ::toolkit::mINI::Instance();
    float duration = ini[Hls::kSegmentDuration];
    int totalSegNum = maxSec / duration - (int)ini[Hls::kSegmentNum] + 1;
    int retain = ini[Hls::kSegmentRetain];
    if (retain < totalSegNum) {
        InfoL << "reset " << Hls::kSegmentRetain << " from " << retain << " to " << totalSegNum;
        ini[Hls::kSegmentRetain] = totalSegNum;
    }
}

float HlsRecorder::duration() const
{
    return _hls->duration();
}

void HlsRecorder::makeSnap(int maxSec, const std::string& dir, SnapCB cb)
{
    _hls->makeSnap(maxSec, dir, cb);
}

bool HlsRecorder::isEnabled()
{
    //������δ���ʱ����������inputFrame�������Ա㼰ʱ��ջ���
    return _option.hls_demand ? (_clear_cache ? true : _enabled) : true;
}

bool HlsRecorder::inputFrame(const Frame::Ptr &frame)
{
    if (_clear_cache && _option.hls_demand) {
        _clear_cache = false;
        //��վɵ�m3u8�����ļ���ts��Ƭ
        _hls->clearCache();
        _hls->getMediaSource()->setIndexFile("");
    }
    if (_enabled || !_option.hls_demand) {
        return MpegMuxer::inputFrame(frame);
    }
    return false;
}

void HlsRecorder::onReaderChanged(MediaSource &sender, int size)
{
    // hls������Ƭ����Ϊ0ʱ����Ϊhls¼��(��ɾ����Ƭ)����ô�������޹ۿ��߶�һֱ����hls
    _enabled = _option.hls_demand ? (_hls->isLive() ? size : true) : true;
    if (!size && _hls->isLive() && _option.hls_demand) {
        // hlsֱ��ʱ��������˹ۿ���ɾ����Ƶ���棬Ŀ����Ϊ�˷�ֹ��Ƶ��Ծ
        _clear_cache = true;
    }
    MediaSourceEventInterceptor::onReaderChanged(sender, size);
}

int HlsRecorder::readerCount()
{
    return _hls->getMediaSource()->readerCount();
}

void HlsRecorder::setListener(const std::weak_ptr<MediaSourceEvent> &listener)
{
    setDelegate(listener);
    _hls->getMediaSource()->setListener(shared_from_this());
}

void HlsRecorder::setMediaSource(const std::string &vhost, const std::string &app, const std::string &stream_id)
{
    _hls->setMediaSource(vhost, app, stream_id);
}

HlsRecorder::~HlsRecorder()
{
    MpegMuxer::flush();
}

HlsRecorder::HlsRecorder(const std::string &m3u8_file, const std::string &params, const ProtocolOption &option) : MpegMuxer(false)
{
    GET_CONFIG(uint32_t, hlsNum, Hls::kSegmentNum);
    GET_CONFIG(bool, hlsKeep, Hls::kSegmentKeep);
    GET_CONFIG(uint32_t, hlsBufSize, Hls::kFileBufSize);
    GET_CONFIG(float, hlsDuration, Hls::kSegmentDuration);

    _option = option;
    _hls = std::make_shared<HlsMakerImp>(m3u8_file, params, hlsBufSize, hlsDuration, hlsNum, hlsKeep);
    //����ϴεĲ����ļ�
    _hls->clearCache();
}
}
