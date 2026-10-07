#pragma once
#include "Arduino.h"
#include <functional>
inline std::deque<std::vector<uint8_t>> fake_wire_packets;
inline std::vector<std::vector<uint8_t>> fake_wire_writes;
inline bool fake_wire_read_error = false, fake_wire_write_error = false;
inline bool fake_wire_begin_ok = true;
inline uint8_t fake_wire_address = 0x4b;
inline uint32_t fake_wire_ready_at = 0;
inline uint8_t fake_wire_probe_rc = 0;
inline std::function<void(const std::vector<uint8_t> &)> fake_wire_on_write;
class TwoWire {
    uint8_t address = 0;
    std::vector<uint8_t> tx;
    std::deque<uint8_t> rx;
    size_t offset = 4;
    bool header_seen = false;
public:
    int sda = -1, scl = -1, port;
    uint32_t hz = 0;
    uint16_t timeout = 0;
    size_t buffer_size = 0;
    explicit TwoWire(int p) : port(p) {}
    size_t setBufferSize(size_t n) { return buffer_size = n; }
    bool begin(int a, int b, uint32_t f) {
        sda=a; scl=b; hz=f; header_seen=false; offset=4; return fake_wire_begin_ok;
    }
    void setTimeOut(uint16_t ms) { timeout=ms; }
    void beginTransmission(uint8_t a) { address=a; tx.clear(); }
    size_t write(const uint8_t *data, size_t n) { tx.insert(tx.end(),data,data+n); return n; }
    uint8_t endTransmission(bool = true) {
        delay(1);
        if (tx.empty() && fake_wire_probe_rc) {
            if (fake_wire_probe_rc == 5) delay(timeout);
            return fake_wire_probe_rc;
        }
        if (address != fake_wire_address || fake_wire_write_error ||
            int32_t(fake_ms - fake_wire_ready_at) < 0) return 2;
        if (!tx.empty()) {
            fake_wire_writes.push_back(tx);
            if (fake_wire_on_write) fake_wire_on_write(tx);
        }
        return 0;
    }
    size_t requestFrom(uint8_t a, uint8_t n, uint8_t) {
        delay(1); rx.clear();
        if (fake_wire_read_error || a != fake_wire_address) { delay(timeout); return 0; }
        if (fake_wire_packets.empty()) { rx.resize(n,0); return n; }
        const auto &p=fake_wire_packets.front();
        if (!header_seen) {
            for (size_t i=0;i<n;++i) rx.push_back(i<p.size() ? p[i] : 0);
            header_seen=true;
        } else {
            for (size_t i=0;i<n;++i) {
                const size_t index=i<4 ? i : offset++;
                rx.push_back(index<p.size() ? p[index] : 0);
            }
            if (offset>=p.size()) { fake_wire_packets.pop_front(); header_seen=false; offset=4; }
        }
        return n;
    }
    int read() { const auto v=rx.front(); rx.pop_front(); return v; }
};
inline TwoWire Wire(0), Wire1(1);
