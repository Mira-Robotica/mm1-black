#pragma once
#include <cstring>
#include <cassert>
constexpr int ESP_LOG_WARN = 2;
inline unsigned fake_i2c_log_enables = 0;
inline void esp_log_level_set(const char *tag, int level) {
    assert(!std::strcmp(tag, "i2c.master") && level == ESP_LOG_WARN);
    ++fake_i2c_log_enables;
}
