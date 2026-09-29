#pragma once
#include <cstdint>
#include <cstddef>

namespace lab {
struct LaserResult { bool valid = false; float distance = 0; const char *error = "LASER_NOT_READY"; };
// Pure parser, shared by firmware and native tests. Only SINGLE (0x20) responses.
class LaserParser {
public:
    bool push(uint8_t byte, LaserResult &result);
    void reset() { count_ = 0; }
private:
    uint8_t frame_[13]{};
    size_t count_ = 0;
};
void laser_begin();
void laser_tick();
void laser_start();
void laser_cancel();
bool laser_done();
LaserResult laser_result();
const char *laser_state();
}
