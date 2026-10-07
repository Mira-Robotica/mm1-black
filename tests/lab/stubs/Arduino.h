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
    template<typename... T> void print(T...) {}
    template<typename... T> void println(T...) {}
};
inline DebugSerial Serial;

// Arduino surface used by the unmodified portions of SparkFun's wrapper.
#include <algorithm>
#include <cmath>
#include <cstring>
template<class A, class B> inline auto min(A a, B b) { return a < b ? a : b; }
using boolean = bool;
using byte = uint8_t;
#define F(s) s
constexpr int HIGH = 1, LOW = 0, OUTPUT = 1, INPUT_PULLUP = 2;
struct PinWrite { int pin, level; uint32_t ms; };
inline std::vector<PinWrite> fake_pin_writes;
inline void (*fake_pin_hook)(int, int) = nullptr;
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int level) {
    fake_pin_writes.push_back({pin, level, fake_ms});
    if (fake_pin_hook) fake_pin_hook(pin, level);
}
inline int digitalRead(int) { return 1; }

using Stream = DebugSerial;
