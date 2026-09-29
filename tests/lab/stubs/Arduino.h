#pragma once
#include <cstdint>
#include <cstddef>
#include <deque>
#include <vector>
#include <cstdio>
inline uint32_t fake_ms = 0;
inline uint32_t fake_us = 0;
inline std::deque<uint8_t> fake_rx;
inline std::vector<uint8_t> fake_tx;
inline uint32_t millis() { return fake_ms; }
inline uint32_t micros() { return fake_us++; }
inline void delay(unsigned ms) { fake_ms += ms; fake_us += ms*1000; }
inline void delayMicroseconds(unsigned us) { fake_us += us; }
constexpr int SERIAL_8N1 = 0;
struct HardwareSerial {
    explicit HardwareSerial(int) {}
    void setRxBufferSize(size_t) {}
    void setTxBufferSize(size_t) {}
    void begin(int, int, int, int) {}
    size_t write(const uint8_t *p, size_t n) { fake_tx.insert(fake_tx.end(), p, p+n); return n; }
    int available() { return fake_rx.size(); }
    int read() { auto b = fake_rx.front(); fake_rx.pop_front(); return b; }
};
struct DebugSerial {
    template<typename... T> void printf(const char *, T...) {}
    void print(const char *) {}
    void println(const char * = "") {}
};
inline DebugSerial Serial;
