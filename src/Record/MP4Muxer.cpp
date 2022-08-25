/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifdef ENABLE_MP4
#include "Util/logger.h"
#include "MP4Muxer.h"
#include "Util/File.h"
#include "Extension/AAC.h"
#include "Extension/H264.h"
#include "Extension/H265.h"
#include "Extension/Factory.h"
#include "Common/config.h"
#include "Ap4.h"

using namespace std;
using namespace toolkit;

const AP4_UI32     AP4_SEGMENT_BUILDER_DEFAULT_TIMESCALE = 1000;
#define AP4_DEFAULT_LANG "und"

namespace mediakit {

bool MP4MuxerInterface::haveVideo() const {
    return _have_video;
}

uint64_t MP4MuxerInterface::getDuration() const {
    uint64_t ret = 0;
    for (auto &pr : _codec_to_trackid) {
        if (pr.second.stamp.getRelativeStamp() > (int64_t)ret) {
            ret = pr.second.stamp.getRelativeStamp();
        }
    }
    return ret;
}

void MP4MuxerInterface::resetTracks() {
    _started = false;
    _have_video = false;
    _frame_merger.clear();
    _codec_to_trackid.clear();
}

void MP4MuxerInterface::flush() {
    _frame_merger.flush();
}

bool MP4MuxerInterface::inputFrame(const Frame::Ptr &frame) {
    auto it = _codec_to_trackid.find(frame->getCodecId());
    if (it == _codec_to_trackid.end()) {
        //该Track不存在或初始化失败
        return false;
    }

    if (!_started) {
        //该逻辑确保含有视频时，第一帧为关键帧
        if (_have_video && !frame->keyFrame()) {
            return false;
        }
        //开始写文件
        _started = true;
    }

    //mp4文件时间戳需要从0开始
    auto &ti = it->second;
    switch (frame->getCodecId()) {
        case CodecH264:
        case CodecH265: {
            //这里的代码逻辑是让SPS、PPS、IDR这些时间戳相同的帧打包到一起当做一个帧处理，
            _frame_merger.inputFrame(frame, [this, &ti](uint64_t dts, uint64_t pts, const Buffer::Ptr &buffer, bool have_idr) {
                int64_t dts_out, pts_out;
                ti.stamp.revise(dts, pts, dts_out, pts_out);
                // store the sample data
                if (_file_stream) {
                    AP4_Position position = 0;
                    _file_stream->Tell(position);
                    _file_stream->Write(buffer->data(), buffer->size());
                    ti.sample_table->AddSample(*_file_stream, position, buffer->size(), 
                        0, 0, dts_out, pts_out - dts_out, have_idr);
                }
                else {
                    AP4_ByteStream* stream = new AP4_MemoryByteStream((AP4_UI08*)buffer->data(), buffer->size());
                    ti.AddSample(AP4_Sample(*stream, 0, buffer->size(), 
                        dts_out - ti.last_tsp, 0, dts_out, pts_out - dts_out, have_idr));
                    stream->Release();
                }
                ti.last_tsp = dts_out;
            });
            break;
        }

        default: {
            int64_t dts_out, pts_out;
            ti.stamp.revise(frame->dts(), frame->pts(), dts_out, pts_out);
            bool key = true;// frame->keyFrame();
            // store the sample data
            if (_file_stream) {
                AP4_Position position = 0;
                _file_stream->Tell(position);
                _file_stream->Write(frame->data() + frame->prefixSize(), frame->size() - frame->prefixSize());
                ti.sample_table->AddSample(*_file_stream, position, frame->size() - frame->prefixSize(), 
                    0, 0, dts_out, pts_out - dts_out, key);
            }
            else {
                int frame_size = frame->size() - frame->prefixSize();
                AP4_ByteStream* stream = new AP4_MemoryByteStream((AP4_UI08*)frame->data() + frame->prefixSize(), frame_size);
                ti.AddSample(AP4_Sample(*stream, 0, frame_size, 
                    dts_out - ti.last_tsp, 0, dts_out, pts_out - dts_out, key));
                stream->Release();
            }
            ti.last_tsp = dts_out;
            break;
        }
    }
    return true;
}

void MP4MuxerInterface::stampSync() {
    if (_codec_to_trackid.size() < 2) {
        return;
    }

    Stamp *audio = nullptr, *video = nullptr;
    for(auto &pr : _codec_to_trackid){
        switch (getTrackType((CodecId) pr.first)){
            case TrackAudio : audio = &pr.second.stamp; break;
            case TrackVideo : video = &pr.second.stamp; break;
            default : break;
        }
    }

    if (audio && video) {
        //音频时间戳同步于视频，因为音频时间戳被修改后不影响播放
        audio->syncTo(*video);
    }
}

bool MP4MuxerInterface::addTrack(const Track::Ptr &track) {
    if (!track->ready()) {
        WarnL << "Track[" << track->getCodecName() << "]未就绪";
        return false;
    }

    auto desc = Factory::getAP4Descripion(track);
    if (!desc) {
        WarnL << "MP4录制不支持该编码格式:" << track->getCodecName();
        return false;
    }

    track_info ti;
    ti.codec = track->getCodecId();
    ti.sample_table = new AP4_SyntheticSampleTable();
    ti.sample_table->AddSampleDescription(desc);
    _codec_to_trackid[track->getCodecId()] = ti;
    //尝试音视频同步
    stampSync();
    return true;
}

/////////////////////////////////////////// MP4Muxer /////////////////////////////////////////////
MP4Muxer::~MP4Muxer() {
    closeMP4();
}

void MP4Muxer::openMP4(const std::string &file) {
    closeMP4();
    _file_name = file;
    InfoL << "openMP4 " << file;
    std::string temp = file + "_";
    File::create_path(File::parentDir(file).c_str(), 0x777);
    AP4_FileByteStream::Create(temp.c_str(), AP4_FileByteStream::STREAM_MODE_WRITE, _file_stream);
}

void MP4Muxer::closeMP4() {
    if (!_file_stream)
        return;
    InfoL << "closeMP4 " << _file_name;
    AP4_ByteStream* stream = nullptr;
    AP4_FileByteStream::Create(_file_name.c_str(), AP4_FileByteStream::STREAM_MODE_WRITE, stream);
    // create the movie object to hold the tracks
    AP4_UI64 creation_time = 0;
    time_t now = time(NULL);
    if (now != (time_t)-1) {
        // adjust the time based on the MPEG time origin
        creation_time = (AP4_UI64)now + 0x7C25B080;
    }
    AP4_Movie* movie = new AP4_Movie(0, 0, creation_time, creation_time);
    // setup the brands
    AP4_Array<AP4_UI32> brands;
    brands.Append(AP4_FILE_BRAND_ISOM);
    brands.Append(AP4_FILE_BRAND_MP42);

    for (auto item : _codec_to_trackid)
    {
        int width = 0;
        int height = 0;
        auto& ti = item.second;
        switch (item.first)
        {
        case CodecH264:
        case CodecH265:
            if (item.first == CodecH264)
                brands.Append(AP4_FILE_BRAND_AVC1);
            else
                brands.Append(AP4_FILE_BRAND_HVC1);
            if(auto* decs = AP4_DYNAMIC_CAST(AP4_VideoSampleDescription, ti.sample_table->GetSampleDescription(0))){
                width = decs->GetWidth();
                height = decs->GetHeight();
            }
            break;
        default:
            break;
        }
        // create a video track
        AP4_Track* track = new AP4_Track(
            getTrackType((CodecId)item.first) == TrackAudio ? AP4_Track::TYPE_AUDIO : AP4_Track::TYPE_VIDEO,
            ti.sample_table,
            0,         // auto-select track id
            AP4_SEGMENT_BUILDER_DEFAULT_TIMESCALE,      // movie time scale
            ti.last_tsp, // track duration
            AP4_SEGMENT_BUILDER_DEFAULT_TIMESCALE,      // media time scale
            1000, // media duration
            AP4_DEFAULT_LANG, // language
            width << 16,      // width
            height << 16      // height
        );
        // update the brands list
        movie->AddTrack(track);
    }
    movie->GetMvhdAtom()->SetNextTrackId(movie->GetTracks().ItemCount() + 1);
    
    // create a multimedia file
    AP4_File file(movie);

    // set the file type
    file.SetFileType(AP4_FILE_BRAND_MP42, 1, &brands[0], brands.ItemCount());

    // write the file to the output
    AP4_FileWriter::Write(file, *stream);
    stream->Release();

    if (_file_stream) {
        _file_stream->Release();
        _file_stream = NULL;

        std::string temp = _file_name + "_";
        remove(temp.c_str());
    }
    MP4MuxerInterface::resetTracks();
}

void MP4Muxer::resetTracks() {
    MP4MuxerInterface::resetTracks();
    openMP4(_file_name);
}

/////////////////////////////////////////// MP4MuxerMemory /////////////////////////////////////////////

MP4MuxerMemory::MP4MuxerMemory() {
}

const string &MP4MuxerMemory::getInitSegment() {
    if (_init_segment.empty()) {
        // create the output file object
        AP4_Movie* output_movie = new AP4_Movie(AP4_SEGMENT_BUILDER_DEFAULT_TIMESCALE);

        // create an mvex container
        AP4_ContainerAtom* mvex = new AP4_ContainerAtom(AP4_ATOM_TYPE_MVEX);
        AP4_MehdAtom* mehd = new AP4_MehdAtom(0);
        mvex->AddChild(mehd);

        // write the ftyp atom
        AP4_Array<AP4_UI32> brands;
        brands.Append(AP4_FILE_BRAND_ISOM);
        brands.Append(AP4_FILE_BRAND_ISO5);
        brands.Append(AP4_FILE_BRAND_MP42);
        brands.Append(AP4_FILE_BRAND_MP41);

        for (auto it = _codec_to_trackid.begin(); it != _codec_to_trackid.end(); it++) {
            auto& ti = it->second;
            AP4_Track* output_track = nullptr;
            if (getTrackType((CodecId)it->first) == TrackAudio) {
                // create a sample table (with no samples) to hold the sample description
                output_track = new AP4_Track(AP4_Track::TYPE_AUDIO, ti.sample_table,
                    0,
                    AP4_SEGMENT_BUILDER_DEFAULT_TIMESCALE,
                    0,
                    ti.m_Timescale,
                    0,
                    AP4_DEFAULT_LANG,
                    0,
                    0);
            }
            else {
                int width = 0;
                int height = 0;
                if (auto* decs = AP4_DYNAMIC_CAST(AP4_VideoSampleDescription, ti.sample_table->GetSampleDescription(0))) {
                    width = decs->GetWidth();
                    height = decs->GetHeight();
                }
                if (it->first == CodecH264)
                    brands.Append(AP4_FILE_BRAND_AVC1);
                if (it->first == CodecH265)
                    brands.Append(AP4_FILE_BRAND_HVC1);
                output_track = new AP4_Track(AP4_Track::TYPE_VIDEO, ti.sample_table,
                    0,
                    AP4_SEGMENT_BUILDER_DEFAULT_TIMESCALE,
                    0,
                    ti.m_Timescale,
                    0,
                    AP4_DEFAULT_LANG,
                    width << 16,
                    height << 16);
            }

            if (output_track) {
                output_movie->AddTrack(output_track);

                ti.m_TrackId = output_track->GetId();
                // add a trex entry to the mvex container
                AP4_TrexAtom* trex = new AP4_TrexAtom(ti.m_TrackId,
                                                    1,
                                                    0,
                                                    0,
                                                    0);
                mvex->AddChild(trex);
            }
        }

        // update the mehd duration
        // TBD mehd->SetDuration(0);

        // the mvex container to the moov container
        output_movie->GetMoovAtom()->AddChild(mvex);

        AP4_MemoryByteStream* stream = new AP4_MemoryByteStream();
        AP4_FtypAtom* ftyp = new AP4_FtypAtom(AP4_FILE_BRAND_MP42, 1, &brands[0], brands.ItemCount());
        ftyp->Write(*stream);
        delete ftyp;

        // write the moov atom
        AP4_Result result = output_movie->GetMoovAtom()->Write(*stream);
        if (AP4_SUCCEEDED(result)) {
            // 输出 ftyp 和 moov
            _init_segment.assign((const char*)stream->GetData(), stream->GetDataSize());
        }
        
        // cleanup
        delete output_movie;
        stream->Release();
    }
    return _init_segment;
}

void MP4MuxerMemory::resetTracks() {
    MP4MuxerInterface::resetTracks();
    _init_segment.clear();
}

bool MP4MuxerMemory::inputFrame(const Frame::Ptr &frame) {
    if (_init_segment.empty()) {
        //尚未生成init segment
        return false;
    }

    if (!MP4MuxerInterface::inputFrame(frame))
        return false;

    auto it = _codec_to_trackid.find(frame->getCodecId());
    if (it == _codec_to_trackid.end()) {
        //该Track不存在或初始化失败
        return false;
    }

    //mp4文件时间戳需要从0开始
    auto& ti = it->second;
    if (!ti.m_Samples.ItemCount())
        return true;

    // flush切片, 输出 moof 和 mdat
    AP4_MemoryByteStream* stream = new AP4_MemoryByteStream();
    ti.WriteMediaSegment(*stream, _seq_no++);

    // write moof         
    std::string data((const char*)stream->GetData(), stream->GetDataSize());
    if (!data.empty()) {
        //输出切片数据(这边用frame的dts,是不是错了?)
        onSegmentData(std::move(data), frame->dts(), frame->keyFrame());
    }
    stream->Release();
    return true;
}

int MP4MuxerInterface::track_info::WriteMediaSegment(AP4_ByteStream& stream, unsigned int sequence_number)
{
    unsigned int tfhd_flags = AP4_TFHD_FLAG_DEFAULT_BASE_IS_MOOF;
    bool isVideo = getTrackType(codec) == TrackVideo;
    if (isVideo) {
        tfhd_flags |= AP4_TFHD_FLAG_DEFAULT_SAMPLE_FLAGS_PRESENT;
    }

    // setup the moof structure
    AP4_ContainerAtom* moof = new AP4_ContainerAtom(AP4_ATOM_TYPE_MOOF);
    AP4_MfhdAtom* mfhd = new AP4_MfhdAtom(sequence_number);
    moof->AddChild(mfhd);

    AP4_ContainerAtom* traf = new AP4_ContainerAtom(AP4_ATOM_TYPE_TRAF);
    AP4_TfhdAtom* tfhd = new AP4_TfhdAtom(tfhd_flags,
                                          m_TrackId,
                                          0,
                                          1,
                                          0,
                                          0,
                                          0);
    if (tfhd_flags & AP4_TFHD_FLAG_DEFAULT_SAMPLE_FLAGS_PRESENT) {
        tfhd->SetDefaultSampleFlags(0x1010000); // sample_is_non_sync_sample=1, sample_depends_on=1 (not I frame)
    }
    traf->AddChild(tfhd);

    AP4_TfdtAtom* tfdt = new AP4_TfdtAtom(1, m_MediaTimeOrigin + m_MediaStartTime);
    traf->AddChild(tfdt);

    AP4_UI32 trun_flags = AP4_TRUN_FLAG_DATA_OFFSET_PRESENT     |
                          AP4_TRUN_FLAG_SAMPLE_DURATION_PRESENT |
                          AP4_TRUN_FLAG_SAMPLE_SIZE_PRESENT;
    AP4_UI32 first_sample_flags = 0;
    if (isVideo && m_Samples[0].IsSync()) {
        trun_flags |= AP4_TRUN_FLAG_FIRST_SAMPLE_FLAGS_PRESENT;
        first_sample_flags = 0x2000000; // sample_depends_on=2 (I frame)
    }
    AP4_TrunAtom* trun = new AP4_TrunAtom(trun_flags, 0, first_sample_flags);
    traf->AddChild(trun);

    moof->AddChild(traf);

    // add samples to the fragment
    AP4_Array<AP4_UI32>            sample_indexes;
    AP4_Array<AP4_TrunAtom::Entry> trun_entries;
    AP4_UI32                       mdat_size = AP4_ATOM_HEADER_SIZE;
    trun_entries.SetItemCount(m_Samples.ItemCount());
    for (unsigned int i = 0; i < m_Samples.ItemCount(); i++) {
        // if we have one non-zero CTS delta, we'll need to express it
        if (m_Samples[i].GetCtsDelta()) {
            trun->SetFlags(trun->GetFlags() | AP4_TRUN_FLAG_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT);
        }

        // add one sample
        AP4_TrunAtom::Entry& trun_entry = trun_entries[i];
        trun_entry.sample_duration                = m_Samples[i].GetDuration();
        trun_entry.sample_size                    = m_Samples[i].GetSize();
        trun_entry.sample_composition_time_offset = m_Samples[i].GetCtsDelta();

        mdat_size += trun_entry.sample_size;
    }

    // update moof and children
    trun->SetEntries(trun_entries);
    trun->SetDataOffset((AP4_UI32)moof->GetSize() + AP4_ATOM_HEADER_SIZE);

    // write moof
    moof->Write(stream);

    // write mdat
    stream.WriteUI32(mdat_size);
    stream.WriteUI32(AP4_ATOM_TYPE_MDAT);
    for (unsigned int i = 0; i < m_Samples.ItemCount(); i++) {
        AP4_Result result;
        AP4_ByteStream* data_stream = m_Samples[i].GetDataStream();
        result = data_stream->Seek(m_Samples[i].GetOffset());
        if (AP4_FAILED(result)) {
            data_stream->Release();
            return result;
        }
        result = data_stream->CopyTo(stream, m_Samples[i].GetSize());
        if (AP4_FAILED(result)) {
            data_stream->Release();
            return result;
        }

        data_stream->Release();
    }

    // update counters
    m_SampleStartNumber += m_Samples.ItemCount();
    m_MediaStartTime    += m_MediaDuration;
    m_MediaDuration      = 0;

    // cleanup
    delete moof;
    m_Samples.Clear();

    return AP4_SUCCESS;
}

AP4_Result MP4MuxerInterface::track_info::AddSample(AP4_Sample sample)
{
    AP4_Result result = m_Samples.Append(sample);
    if (AP4_FAILED(result)) return result;
    m_MediaDuration += sample.GetDuration();

    return AP4_SUCCESS;
}

}//namespace mediakit
#endif//#ifdef ENABLE_MP4
