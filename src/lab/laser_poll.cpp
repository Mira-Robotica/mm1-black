#include "laser_poll.h"
#include "settings.h"
#include "board/p4/mm1_p4_pins.h"
#include <Arduino.h>
#include <cstring>

namespace lab {
namespace {
HardwareSerial port(MM1_LZR_UART_NUM);
constexpr uint8_t on[] = {0xAA,0,1,0xBE,0,1,0,1,0xC1};
constexpr uint8_t off[] = {0xAA,0,1,0xBE,0,1,0,0,0xC0};
constexpr uint8_t single[] = {0xAA,0,0,0x20,0,1,0,0,0x21};
enum class Phase { Idle, Drain, Warmup, Wait, Done };
Phase phase = Phase::Idle;
LaserParser parser;
LaserResult result;
uint32_t since = 0, last_rx = 0;
bool uncertain = false;
void send(const uint8_t *data) { port.write(data, 9); }
void finish(const char *error = nullptr)
{
    if (error) { result.valid = false; result.error = error; }
    send(off);
    phase = Phase::Done;
}
}
void laser_begin()
{
    uncertain = false; phase = Phase::Idle; parser.reset(); result = {};
    port.setRxBufferSize(1024);
    port.setTxBufferSize(128); // 9-byte writes queue; no flush()/delay() in polling.
    port.begin(9600, SERIAL_8N1, MM1_LZR_RX, MM1_LZR_TX);
    send(off);
    since = last_rx = millis();
}
const char *laser_state() { return uncertain ? "RESYNC_REQUIRED" : "READY"; }
void laser_start()
{
    result = {};
    parser.reset();
    if (uncertain) { finish("LASER_RESYNC_REQUIRED"); return; }
    since = last_rx = millis();
    send(off);
    phase = Phase::Drain;
}
void laser_cancel()
{
    if (phase == Phase::Wait) uncertain = true;
    send(off);
    parser.reset();
    phase = Phase::Idle;
}
void laser_tick()
{
    uint32_t now = millis();
    // Check before RX: an overdue reply is never accepted as a timely sample.
    if (phase == Phase::Wait && now-since >= LAB_LASER_TIMEOUT_MS) {
        uncertain = true;
        finish(!strcmp(result.error, "LASER_CHECKSUM") ? "LASER_CHECKSUM" : "LASER_TIMEOUT");
    }
    if (phase == Phase::Wait && now-last_rx >= 250) parser.reset();
    for (unsigned count = 0; count < 256 && port.available(); ++count) {
        const uint8_t byte = port.read();
        last_rx = now;
        if (phase == Phase::Wait && parser.push(byte, result)) {
            if (!result.valid) uncertain = true;
            finish();
        }
    }
    if (phase == Phase::Drain) {
        if (now-since >= 500) { uncertain = true; finish("LASER_RX_BUSY"); }
        else if (now-last_rx >= 50 && !port.available()) {
            send(on); since = now; phase = Phase::Warmup;
        }
    } else if (phase == Phase::Warmup && now-since >= 120) {
        if (port.available()) return;
        parser.reset(); result.error = "LASER_TIMEOUT";
        send(single); since = now; phase = Phase::Wait;
    }
}
bool laser_done() { return phase == Phase::Done; }
LaserResult laser_result() { return result; }
}
