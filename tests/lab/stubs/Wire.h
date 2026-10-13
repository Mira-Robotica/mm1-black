#pragma once
#include "Arduino.h"
#include <functional>
#include <cassert>
inline std::deque<std::vector<uint8_t>> fake_wire_packets;
inline std::vector<std::vector<uint8_t>> fake_wire_writes;
inline bool fake_wire_read_error = false, fake_wire_write_error = false;
inline bool fake_wire_begin_ok = true;
inline bool fake_wire_end_ok = true;
inline std::function<void(std::deque<uint8_t> &, size_t)> fake_wire_on_read;
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
    bool initialized = false, needs_end = false;
    unsigned begins = 0, ends = 0;
    int sda = -1, scl = -1, port;
    uint32_t hz = 0;
    uint16_t timeout = 0;
    size_t buffer_size = 0;
    explicit TwoWire(int p) : port(p) {}
    size_t setBufferSize(size_t n) { return buffer_size = n; }
    bool begin(int a, int b, uint32_t f) {
        ++begins;
        if (initialized) return true; // Arduino does not reinitialize an open bus.
        sda=a; scl=b; hz=f; header_seen=false; offset=4;
        return initialized=fake_wire_begin_ok;
    }
    bool end() {
        ++ends;
        if (!fake_wire_end_ok) return false;
        initialized=needs_end=false; tx.clear(); rx.clear();
        header_seen=false; offset=4;
        return true;
    }
    void setTimeOut(uint16_t ms) { timeout=ms; }
    void beginTransmission(uint8_t a) { address=a; tx.clear(); }
    size_t write(const uint8_t *data, size_t n) { tx.insert(tx.end(),data,data+n); return n; }
    uint8_t endTransmission(bool = true) {
        assert(initialized);
        assert(!tx.empty() || !needs_end); // A new probe must not reuse failed RX state.
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
        assert(initialized && n <= buffer_size);
        delay(1); rx.clear();
        if (fake_wire_read_error || a != fake_wire_address) { needs_end=true; delay(timeout); return 0; }
        if (fake_wire_packets.empty()) { rx.resize(n,0); return n; }
        const auto &p=fake_wire_packets.front();
        if (!header_seen) {
            for (size_t i=0;i<n;++i) rx.push_back(i<p.size() ? p[i] : 0);
            header_seen=true;
        } else {
            const size_t remaining = p.size() - offset + 4;
            for (size_t i=0;i<n;++i) {
                const size_t index=i<4 ? i : offset++;
                if (i==0) rx.push_back(uint8_t(remaining));
                else if (i==1) rx.push_back(uint8_t(remaining>>8) | 0x80);
                else rx.push_back(index<p.size() ? p[index] : 0);
            }
            if (offset>=p.size()) { fake_wire_packets.pop_front(); header_seen=false; offset=4; }
        }
        if (fake_wire_on_read) fake_wire_on_read(rx, n);
        return n;
    }
    int read() { const auto v=rx.front(); rx.pop_front(); return v; }
};
inline TwoWire Wire(0), Wire1(1);
