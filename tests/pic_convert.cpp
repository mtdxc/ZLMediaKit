#include "Codec/Transcode.h"
#include "Extension/Frame.h"
#include "Extension/Track.h"
#include "Extension/Factory.h"
#include "ext-codec/H264.h"
#include "Util/File.h"
using namespace mediakit;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <src> <dest>\n", argv[0]);
        return -1;
    }

    CodecId src = CodecJPEG; // 默认为JPEG
    char* dot = strchr(argv[1], '.');
    if (dot) {
        src = getCodecId(dot + 1);
    }

    CodecId dst = CodecInvalid;
    dot = strchr(argv[2], '.');
    if (dot) {
        dst = getCodecId(dot + 1);
    }
    if (dst == CodecInvalid) {
        printf("Unsupported codec %s\n", dot + 1);
        return -1;
    }
    FFmpegDecoder decoder(Factory::getTrackByCodecId(src), 1);
    decoder.setOnDecode([=](const FFmpegFrame::Ptr &frame) {
        if (frame) {
            bool done = false;
            printf("Decoded frame: codec=%d, width=%d, height=%d\n", frame->format, frame->width, frame->height);
            auto cfg = std::make_shared<VideoTrackImp>(dst, frame->width, frame->height, 30);
            FFmpegEncoder encoder(cfg, 1);
            encoder.setOnEncode([=, &done](const Frame::Ptr &encoded_frame) {
                if (encoded_frame) {
                    printf("Encoded frame: codec=%d, size=%zu\n", encoded_frame->getCodecId(), encoded_frame->size());
                    if (!done) {
                        FILE *fp = fopen(argv[2], "wb");
                        if (fp) {
                            if (dst == CodecH264 || dst == CodecH265) {
                                splitH264(encoded_frame->data(), encoded_frame->size(), encoded_frame->prefixSize(), [=](const char *data, size_t size, size_t prefix_size) {
                                    auto frame = Factory::getFrameFromPtr(dst, data, size, 0, 0);
                                    if (frame->dropAble()) {
                                        printf("skip nal %u size %zu\n", data[prefix_size], size);
                                        return;
                                    }
                                    fwrite(data, 1, size, fp);
                                });
                            } else {
                                // 其他格式直接保存
                                fwrite(encoded_frame->data(), 1, encoded_frame->size(), fp);
                            }
                            fclose(fp);
                        } 
                        done = true;
                    }
                } else {
                    printf("No frame encoded\n");
                }
            });

            while (!done) {
                encoder.inputFrame(frame, false);
                frame->pts += 30; // 模拟时间戳递增
            }
        } else {
            printf("No frame decoded\n");
        }
    });

    std::string buff = toolkit::File::loadFile(argv[1]);
    decoder.inputFrame(Factory::getFrameFromPtr(src, buff.data(), buff.size(), 0, 0), false, false);
    //auto frame = std::make_shared<FrameFromPtr>(src, (char*)buff.data(), buff.size(), 0, 0, (src == CodecH264 || src == CodecH265)?4:0, true);
    //decoder.inputFrame(frame, false, false);
    return 0;
}
