#include <stdio.h>
#include "Record/MP4Demuxer.h"
using namespace mediakit;

void useage(char* exe) {
    printf("usage: %s\tinfo <mp4 path>...\n", exe);
    printf("\tdump <mp4 path> [0/1 TrackType]\n");
    printf("\tdumpAudio <mp4 path>...\n", exe);
    printf("\tdumpVideo <mp4 path>...\n", exe);
    printf("\tcopy <src mp4 path> <dst mp4 path> [0/1]\n");
    printf("\tcopyAudio <src mp4 path> <dst mp4 path> [0/1]\n");
    printf("\tcopyVideo <src mp4 path> <dst mp4 path> [0/1]\n");
    printf("\tcopyRaw <src mp4 path> <dst mp4 path> [0/1]\n");
    printf("\tcopyRawAudio <src mp4 path> <dst mp4 path> [0/1]\n");
    printf("\tcopyRawVideo <src mp4 path> <dst mp4 path> [0/1]\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        useage(argv[0]);
        return 0;
    }

    std::string cmd = argv[1];
    if (cmd == "info") {
        for (int i = 2; i < argc; i++) {
            mp4Dump(argv[i], TrackInvalid);
        }
    } else if (cmd == "dumpAudio") {
        for (int i = 2; i < argc; i++) {
            mp4Dump(argv[i], TrackAudio);
        }
    } else if (cmd == "dumpVideo") {
        for (int i = 2; i < argc; i++) {
            mp4Dump(argv[i], TrackVideo);
        }
    }
    else if (cmd == "dump") {
        if (argc < 3) {
            printf("usage: %s %s <mp4 path> [0/1 TrackType]\n", argv[0], argv[1]);
            return -1;
        }
        int type = TrackMax;
        if (argc > 3) {
            type = atoi(argv[3]);
        }
        mp4Dump(argv[2], (TrackType)type);
    }
    else if(cmd == "copyAudio") {
        if (argc < 4) {
            printf("usage: %s %s <src mp4 path> <dst mp4 path> [0/1]\n", argv[0], argv[1]);
            return -1;
        }
        splitMp4(argv[2], argv[3], TrackAudio);
    }
    else if(cmd == "copyVideo") {
        if (argc < 4) {
            printf("usage: %s %s <src mp4 path> <dst mp4 path> [0/1]\n", argv[0], argv[1]);
            return -1;
        }
        splitMp4(argv[2], argv[3], TrackVideo);
    }
    else if(cmd == "copy") {
        if (argc < 4) {
            printf("usage: %s %s <src mp4 path> <dst mp4 path> [0/1]\n", argv[0], argv[1]);
            return -1;
        }
        int flags = 0;
        if (argc > 4) {
            flags = atoi(argv[4]);
        }
        copyMp4(argv[2], argv[3], flags);
    }
    else if(cmd == "copyRaw") {
        if (argc < 4) {
            printf("usage: %s %s <src mp4 path> <dst mp4 path> [0/1]\n", argv[0], argv[1]);
            return -1;
        }
        int flags = 0;
        if (argc > 4) {
            flags = atoi(argv[4]);
        }
        copyMp4Raw(argv[2], argv[3], flags, TrackMax);
    } else if (cmd == "copyRawAudio") {
        if (argc < 4) {
            printf("usage: %s %s <src mp4 path> <dst mp4 path> [0/1]\n", argv[0], argv[1]);
            return -1;
        }
        int flags = 0;
        if (argc > 4) {
            flags = atoi(argv[4]);
        }
        copyMp4Raw(argv[2], argv[3], flags, TrackAudio);
    } else if (cmd == "copyRawVideo") {
        if (argc < 4) {
            printf("usage: %s %s <src mp4 path> <dst mp4 path> [0/1]\n", argv[0], argv[1]);
            return -1;
        }
        int flags = 0;
        if (argc > 4) {
            flags = atoi(argv[4]);
        }
        copyMp4Raw(argv[2], argv[3], flags, TrackVideo);
    }    
    else if(cmd == "dropVideo") {
        if (argc < 4) {
            printf("usage: %s %s <src mp4 path> <dst mp4 path> <start ms> <end ms>\n", argv[0], argv[1]);
            return -1;
        }
        uint64_t start = 0;
        uint64_t end = 0;
        if (argc > 4) {
            start = atoll(argv[4]);
        }
        if (argc > 5) {
            end = atoll(argv[5]);
        }
        Mp4DropVideo(argv[2], argv[3], start, end);
    }
    return 0;
}