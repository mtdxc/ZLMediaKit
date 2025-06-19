
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <set>
#include <functional>

#define H264_TYPE(v) ((uint8_t)(v)&0x1F)
#define H265_TYPE(v) (((uint8_t)(v) >> 1) & 0x3f)
static const char *memfind(const char *buf, size_t len, const char *subbuf, size_t sublen) {
    for (size_t i = 0; i < len - sublen; ++i) {
        if (memcmp(buf + i, subbuf, sublen) == 0) {
            return buf + i;
        }
    }
    return NULL;
}

void splitNal(const char *ptr, size_t len, size_t prefix, const std::function<void(const char *, size_t, size_t)> &cb) {
    auto start = ptr + prefix;
    auto end = ptr + len;
    size_t next_prefix;
    while (true) {
        auto next_start = memfind(start, end - start, "\x00\x00\x01", 3);
        if (next_start) {
            // Find the next frame
            if (*(next_start - 1) == 0x00) {
                // This starts with 00 00 00 01
                next_start -= 1;
                next_prefix = 4;
            } else {
                // This starts with 00 00 01
                next_prefix = 3;
            }
            // Remember to add the prefix length of this frame
            cb(start - prefix, next_start - start + prefix, prefix);
            // Search for the starting position of the end of the next frame
            start = next_start + next_prefix;
            // Record the prefix length of the next frame
            prefix = next_prefix;
            continue;
        }
        // The next frame was not found, this is the last frame
        cb(start - prefix, end - start + prefix, prefix);
        break;
    }
}

void remove_sei(const char* src, const char* dest, bool h265) {
    FILE* in = fopen(src, "rb");
    if (!in) {
        printf("fopen %s error\n", src);
        return;
    }
    FILE* out = fopen(dest, "wb");
    if (!out) {
        printf("fopen %s error\n", dest);
        fclose(in);
        return ;
    }
    fseek(in, 0, SEEK_END);
    size_t len = ftell(in);
    fseek(in, 0, SEEK_SET);
    char* buf = new char[len];
    fread(buf, 1, len, in);
    fclose(in);

    splitNal(buf, len, 0, [out, h265](const char *data, size_t len, size_t prefix) {
        if (h265) {
            uint8_t type = H265_TYPE(data[prefix]);
            if (type == 35 || type == 39 || type == 40) {
                printf("remove %d %zu\n", type, len);
                return;
            }
        } else {
            uint8_t type = H264_TYPE(data[prefix]);
            if (type == 6 || type == 9) {
                printf("remove %d %zu\n", type, len);
                return;
            }
        }
        fwrite(data, 1, len, out);
    });
    delete[] buf;
    fclose(out);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <src.h264> <dest.h264> [h265]\n", argv[0]);
        return -1;
    }
    bool h265 = false;
    if (argc == 4) {
        h265 = atoi(argv[3]);
    }
    remove_sei(argv[1], argv[2], h265);
    return 0;
}