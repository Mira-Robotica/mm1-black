#include "laser_poll.h"
#include <cstring>

namespace lab {
bool LaserParser::push(uint8_t byte, LaserResult &out)
{
    if (!count_ && byte != 0xAA) return false;
    frame_[count_++] = byte;
    if (count_ < sizeof(frame_)) return false;
    uint8_t sum = 0;
    for (size_t i = 1; i < 12; ++i) sum += frame_[i];
    const bool checksum = sum == frame_[12];
    const bool single = frame_[3] == 0x20;
    bool complete = false;
    if (single) {
        out = {};
        out.error = "LASER_CHECKSUM";
        if (checksum) {
            complete = true;
            out.error = "LASER_FRAME";
            // Preserve the existing documented length layout, reject permissive fallback.
            // No documented status mapping for bytes 1/2 or 10/11 in the
            // existing driver. Do not invent sensor-error meanings for them.
            if (frame_[4] == 0 && frame_[5] == 4) {
                uint32_t mm = 0;
                bool bcd = true;
                for (size_t i = 6; i < 10; ++i) {
                    const unsigned hi = frame_[i] >> 4, lo = frame_[i] & 15;
                    bcd = bcd && hi <= 9 && lo <= 9;
                    mm = mm*100 + hi*10 + lo;
                }
                out.error = bcd ? "LASER_RANGE" : "LASER_BCD";
                if (bcd && mm > 0) {
                    out.valid = true;
                    out.distance = mm*0.001f;
                    out.error = "NONE";
                }
            }
        }
    }
    if (checksum) count_ = 0;
    else {
        size_t next = 1;
        while (next < 13 && frame_[next] != 0xAA) ++next;
        count_ = 13-next;
        std::memmove(frame_, frame_+next, count_);
    }
    return complete;
}
}
