#include "network.h"

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <errno.h>
#include <esp32-hal-hosted.h>
#include <esp_random.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <string.h>

#include "line_reader.h"
#include "settings.h"
#include "capture_service.h"
#include "calibration_service.h"
#include "stream_service.h"

static_assert(LAB_TCP_PORT > 0 && LAB_TCP_PORT <= 65535, "Invalid TCP port");
static_assert(LAB_WIFI_CONNECT_TIMEOUT_MS > 0 && LAB_WIFI_CONNECT_TIMEOUT_MS < 0x80000000UL,
              "Invalid Wi-Fi timeout");
static_assert(LAB_WIFI_RETRY_MS > 0 && LAB_WIFI_RETRY_MS < 0x80000000UL,
              "Invalid Wi-Fi retry interval");

namespace {
enum class State { ConfigRequired, Waiting, Connecting, Connected, Fault };
State state = State::Waiting;
uint32_t state_since = 0;
bool first_attempt = true;
bool radio_ready = false;
bool server_started = false;
uint32_t attempt = 0;
uint32_t connection_id = 0;
char boot_id[17]{};

WiFiServer server(LAB_TCP_PORT, 1);
WiFiClient client;
LabLineReader tcp_line;
LabLineReader serial_line;
char tx[2048]{};
char control[2048]{};
bool tx_snapshot=false;
enum class Owner { None, Tcp, Serial };
Owner owner = Owner::None;
bool result_queued = false;
uint32_t last_tcp_id = 0, last_serial_id = 0;
size_t tx_size = 0;
size_t tx_sent = 0;
uint32_t tx_since = 0;
uint32_t client_activity = 0;
uint32_t server_retry_since = 0;
std::atomic<bool> link_lost{false};
std::atomic<unsigned> disconnect_reason{0};

const char *state_name()
{
    switch (state) {
    case State::ConfigRequired: return "CONFIG_REQUIRED";
    case State::Waiting: return "RETRY_WAIT";
    case State::Connecting: return "CONNECTING";
    case State::Connected: return "CONNECTED";
    case State::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

void close_client()
{
    lab::calibration_disconnect(lab::CalOwner::Tcp);
    lab::stream_disconnect(connection_id);
    control[0]=0;
    if (owner == Owner::Tcp) {
        lab::capture_cancel(); owner = Owner::None; result_queued = false;
    }
    last_tcp_id = 0;
    client.stop();
    tcp_line.reset();
    tx_size = tx_sent = 0; tx_snapshot=false;
}

void stop_server()
{
    close_client();
    server.end();
    server_started = false;
}

void queue_reply(const char *reply)
{
    // One bounded control reply may wait behind the line already being sent.
    const size_t len = strlen(reply);
    if (len >= sizeof(tx) || control[0]) {
        close_client();
        return;
    }
    if (tx_sent != tx_size) { memcpy(control,reply,len+1); return; }
    memcpy(tx, reply, len);
    tx_size = len;
    tx_snapshot = false;
    tx_sent = 0;
    tx_since = millis();
}

void format_status(char *out, size_t size)
{
    const bool online = state == State::Connected;
    const String ip = online ? WiFi.localIP().toString() : String("0.0.0.0");
    const auto imu = p4_imu_diagnostics();
    const auto product = p4_imu_lab_info();
    snprintf(out, size,
             "# OK STATUS stage=multi_report_capture wifi=%s ip=%s rssi_dbm=%ld "
             "tcp=%s port=%u hosted=%u sensors=ENABLED "
             "attempt=%lu disconnect_reason=%u uptime_ms=%lu state=%s laser=%s imu=%s "
             "imu_reports=%lu imu_resets=%lu imu_io_errors=%lu imu_decode_errors=%lu imu_sequence_gaps=%lu n_max=1 calibration=%s calibration_blocked=%u "
             "imu_init=%s imu_init_rc=%d imu_addr=0x%02X " MM1_LAB_IMU_META
             "imu_hw_resets=%lu imu_init_ms=%lu imu_part=%lu imu_fw=%u.%u.%u imu_build=%lu "
             "imu_probe_4b_rc=%d imu_probe_4a_rc=%d imu_probe_attempts=%lu imu_probe_ms=%lu "
             "imu_probe_sda=%d imu_probe_scl=%d imu_rst_low=%d imu_rst_high=%d "
             "imu_epoch=%lu grv_reports=%lu mag_reports=%lu grv_sequence_gaps=%lu mag_sequence_gaps=%lu "
             "rv_us=%lu grv_us=%lu mag_us=%lu rv_confirmed_us=%ld grv_confirmed_us=%ld mag_confirmed_us=%ld stream_state=%s stream_session_id=%lu\n",
             state_name(), ip.c_str(), online ? static_cast<long>(WiFi.RSSI()) : 0L,
             server_started ? "LISTENING" : "OFF", static_cast<unsigned>(LAB_TCP_PORT),
             static_cast<unsigned>(hostedIsInitialized()),
             static_cast<unsigned long>(attempt), disconnect_reason.load(),
             static_cast<unsigned long>(millis()), lab::calibration_blocks_capture() ? "CALIBRATING" : lab::capture_state(), lab::laser_state(),
             imu.ready ? "READY" : "NOT_READY", (unsigned long)imu.generation,
             (unsigned long)imu.resets, (unsigned long)imu.io_errors,
             (unsigned long)imu.decode_errors, (unsigned long)imu.sequence_gaps,
             lab::calibration_state(), (unsigned)lab::calibration_blocks_capture(),
             imu.init_stage, imu.init_rc, imu.address,
             (unsigned long)imu.hardware_resets, (unsigned long)imu.init_ms,
             (unsigned long)product.part, product.major, product.minor, product.patch,
             (unsigned long)product.build, imu.probe_4b_rc, imu.probe_4a_rc,
             (unsigned long)imu.probe_attempts, (unsigned long)imu.probe_elapsed_ms,
             imu.probe_sda, imu.probe_scl, imu.rst_low, imu.rst_high,
             (unsigned long)imu.epoch, (unsigned long)imu.game_generation, (unsigned long)imu.mag_generation,
             (unsigned long)product.game.sequence_gaps, (unsigned long)product.mag.sequence_gaps,
             (unsigned long)product.rv_interval_us, (unsigned long)product.game_interval_us,
             (unsigned long)product.mag_interval_us, (long)product.rv_confirmed_us,
             (long)product.game_confirmed_us, (long)product.mag_confirmed_us, lab::stream_state(), (unsigned long)lab::stream_session_id());
}

void command(const char *line, bool from_serial)
{
    char reply[sizeof(tx)];
    if (strcmp(line, "STATUS") == 0) {
        format_status(reply, sizeof(reply));
    } else if (strncmp(line, "STREAM_IMU", 10) == 0) {
        if (from_serial) snprintf(reply,sizeof(reply),"# ERR STREAM_IMU code=TCP_ONLY\n");
        else lab::stream_command(line,connection_id,reply,sizeof(reply));
    } else if (lab::stream_busy()) {
        snprintf(reply,sizeof(reply),"# ERR BUSY\n");
    } else if (strcmp(line, "IMU_RESET") == 0) {
        if (lab::capture_busy() || lab::calibration_busy()) {
            snprintf(reply, sizeof(reply), "# ERR BUSY\n");
        } else {
            p4_imu_request_reset();
            snprintf(reply, sizeof(reply), "# OK IMU_RESET state=SCHEDULED\n");
        }
    } else if (strcmp(line, "CAL_IMU") == 0 || strncmp(line, "CAL_IMU ", 8) == 0) {
        lab::calibration_command(line, from_serial ? lab::CalOwner::Serial : lab::CalOwner::Tcp, reply, sizeof(reply));
    } else if (strcmp(line, "CAPTURE") == 0 || strncmp(line, "CAPTURE ", 8) == 0 ||
               strcmp(line, "CAPTURE_IMU") == 0 || strncmp(line, "CAPTURE_IMU ", 12) == 0) {
        uint32_t id = 0, count = 0;
        bool use_laser = true;
        uint32_t &last = from_serial ? last_serial_id : last_tcp_id;
        if (!lab::parse_capture(line, id, count, &use_laser)) {
            snprintf(reply, sizeof(reply), "# ERR BAD_CAPTURE expected=CAPTURE_or_CAPTURE_IMU_id_[1]\n");
        } else if (count != 1) {
            snprintf(reply, sizeof(reply), "# ERR N_RANGE n_max=1\n");
        } else if (lab::capture_busy() || lab::calibration_blocks_capture()) {
            snprintf(reply, sizeof(reply), "# ERR BUSY\n");
        } else if (id <= last) {
            snprintf(reply, sizeof(reply), "# ERR DUPLICATE_ID\n");
        } else if (lab::capture_start(id, use_laser)) {
            last = id; owner = from_serial ? Owner::Serial : Owner::Tcp;
            result_queued = false;
            snprintf(reply, sizeof(reply), "# OK %s request_id=%lu n=1 timeout_ms=9000\n",
                     use_laser ? "CAPTURE" : "CAPTURE_IMU", (unsigned long)id);
        } else snprintf(reply, sizeof(reply), "# ERR BUSY\n");
    } else {
        snprintf(reply, sizeof(reply), "# ERR BAD_COMMAND supported=STATUS,CAPTURE,CAPTURE_IMU,CAL_IMU,IMU_RESET\n");
    }
    if (from_serial) {
        Serial.print(reply);
    } else {
        if (reply[0]) queue_reply(reply);
    }
}

void service_serial()
{
    for (size_t count = 0; count < 128 && Serial.available(); ++count) {
        const auto result = serial_line.push(static_cast<char>(Serial.read()));
        if (result == LabLineReader::Result::Line) {
            command(serial_line.line(), true);
        } else if (result == LabLineReader::Result::TooLong) {
            Serial.println("# ERR LINE_TOO_LONG");
        } else if (result == LabLineReader::Result::Invalid) {
            Serial.println("# ERR BAD_COMMAND");
        }
    }
}

void service_tcp()
{
    if (!server_started) {
        return;
    }
    // Refuse extra clients rather than replacing the current connection.
    WiFiClient incoming = server.accept();
    if (incoming) {
        if (client.connected()) {
            incoming.stop();
        } else {
            close_client();
            client = incoming;
            client.setNoDelay(true);
            client_activity = millis();
            ++connection_id;
            char hello[sizeof(tx)];
            snprintf(hello, sizeof(hello),
                     "# HELLO MM1LAB 3 stage=multi_report_capture boot_id=%s connection_id=%lu\n"
                     "# META capture=single commands=STATUS,CAPTURE,CAPTURE_IMU,CAL_IMU,IMU_RESET,STREAM_IMU stream_imu=1 stream_schema=1 cal_schema=1 cal_line_max=2048 schema=3 n_max=1 timeout_ms=9000 "
                     "imu_reports=rotation_vector,game_rotation_vector,magnetic_field_calibrated "
                     "rv_id=0x05 grv_id=0x08 mag_id=0x03 rv_requested_us=20000 grv_requested_us=20000 mag_requested_us=20000 "
                     "intervals=nominal_acquisition confirmed_intervals=STATUS unknown_interval=-1 "
                     "quaternion_order=wxyz grv_heading_reference=arbitrary mag_units=uT mag_frame=sensor_output "
                     "orientation_convention=body_to_world rv_world=ENU imu_epoch_scope=boot_id "
                     MM1_LAB_IMU_META
                     "laser_axis=%.9g/%.9g/%.9g azimuth_reference=magnetic azimuth_offset_deg=0 "
                     "distance_source=laser_report laser_trim_mm=0 quaternion_norm_tolerance=0.02 "
                     "singularity_epsilon=1e-5 csv_header=per_capture\n",
                     boot_id, static_cast<unsigned long>(connection_id),
                     (double)IMU_LASER_AXIS_BX, (double)IMU_LASER_AXIS_BY, (double)IMU_LASER_AXIS_BZ);
            queue_reply(hello);
        }
    }
    if (!client.connected()) {
        close_client();
        return;
    }
    if (millis() - client_activity >= 60000UL) {
        close_client();
        return;
    }
    // Continue reading STOP/PING while a bounded partial line is in flight.
    // Control waits behind that complete line; bytes are never interleaved.
    if (!control[0]) {
        for (size_t count = 0; count < 128 && client.available(); ++count) {
            const auto result = tcp_line.push(static_cast<char>(client.read()));
            client_activity = millis();
            if (result == LabLineReader::Result::Line) {
                command(tcp_line.line(), false);
                break;
            }
            if (result == LabLineReader::Result::TooLong || result == LabLineReader::Result::Invalid) {
                queue_reply(result == LabLineReader::Result::TooLong
                                ? "# ERR LINE_TOO_LONG\n" : "# ERR BAD_COMMAND\n");
                break;
            }
        }
    }
    if (tx_sent == tx_size && control[0]) {
        char pending[sizeof(control)]; strcpy(pending,control); control[0]=0;
        queue_reply(pending);
    }
    if (tx_snapshot && tx_sent==0 && !control[0]) {
        if (strcmp(lab::stream_state(),"STREAMING")) {
            tx_size=0; tx_snapshot=false; // No byte of this snapshot has left the device.
        } else {
            char latest[sizeof(tx)];
            if (lab::stream_output(latest,sizeof(latest),true)) {
                // Replace only a completely unstarted snapshot, retaining novelty bits.
                const uint32_t since=tx_since;
                tx_size=tx_sent=0; queue_reply(latest); tx_snapshot=true; tx_since=since;
            }
        }
    }
    if (tx_sent < tx_size) {
        // NetworkClient::write can retry/block. Send a small reply without waiting.
        const int sent = ::send(client.fd(), tx + tx_sent, tx_size - tx_sent, MSG_DONTWAIT);
        if (sent > 0) {
            tx_sent += static_cast<size_t>(sent);
            if (tx_sent == tx_size && result_queued) {
                result_queued = false; lab::capture_cancel(); owner = Owner::None;
            }
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            close_client();
        }
        if (tx_sent < tx_size && millis() - tx_since >= 2000UL) {
            close_client();
        }
        return;
    }
    if (owner == Owner::Tcp && lab::capture_ready()) {
        char response[sizeof(tx)];
        if (!lab::format_sample(response, sizeof(response), lab::capture_sample())) {
            close_client(); return;
        }
        queue_reply(response); result_queued = true;
        return;
    }
    if (tx_sent == tx_size && !control[0]) {
        char output[sizeof(tx)];
        if (lab::stream_output(output,sizeof(output))) {
            queue_reply(output); tx_snapshot=output[0]>='0' && output[0]<='9';
        }
    }

}

void start_server()
{
    server_retry_since = millis();
    server.begin();
    server_started = static_cast<bool>(server);
    Serial.printf("[LAB] TCP port=%u %s\n", static_cast<unsigned>(LAB_TCP_PORT),
                  server_started ? "LISTENING (STATUS,CAPTURE/CAPTURE_IMU n=1)" : "FAILED; retry in 10 s");
}

void start_attempt()
{
    ++attempt;
    Serial.printf("[WiFi] attempt=%lu STA SSID=\"%s\"\n",
                  static_cast<unsigned long>(attempt), LAB_WIFI_SSID);
    // Configure mode once. Never tear down Hosted with WIFI_OFF during retries.
    if (!radio_ready) {
        if (!WiFi.mode(WIFI_STA)) {
            Serial.println("[WiFi] STA/ESP-Hosted init failed; check power and the SDIO link.");
            state = State::Waiting;
            state_since = millis();
            return;
        }
        radio_ready = true;
        if (!WiFi.setSleep(false)) {
            Serial.println("[WiFi] modem sleep setting was not accepted.");
        }
        Serial.printf("[C6] hosted=%u\n", static_cast<unsigned>(hostedIsInitialized()));
    }
    WiFi.begin(LAB_WIFI_SSID, LAB_WIFI_PASSWORD[0] ? LAB_WIFI_PASSWORD : nullptr);
    state = State::Connecting;
    state_since = millis();
}
} // namespace

namespace lab {
void network_begin()
{
    snprintf(boot_id, sizeof(boot_id), "%08lx%08lx",
             static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
    Serial.println("[LAB] Send STATUS on the serial monitor (newline) for diagnostics.");
    if (strlen(LAB_WIFI_SSID) == 0 || strlen(LAB_WIFI_SSID) > 32 ||
        strlen(LAB_WIFI_PASSWORD) > 64) {
        state = State::ConfigRequired;
        Serial.println("[LAB] CONFIG_REQUIRED: edit include/lab_config.h, then build/upload again.");
        return;
    }
    // Waveshare SDIO: CLK, CMD, D0, D1, D2, D3, RESET.
    if (!hostedSetPins(18, 19, 14, 15, 16, 17, 54)) {
        state = State::Fault;
        Serial.println("[C6] Failed to configure SDIO pins before Wi-Fi initialization.");
        return;
    }
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false); // This loop owns the timeout/retry policy.
    WiFi.setHostname(LAB_HOSTNAME);
    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
        // Event callbacks run on a separate task; leave socket operations to loop().
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            disconnect_reason.store(info.wifi_sta_disconnected.reason);
            link_lost.store(true);
        } else if (event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
            link_lost.store(true);
        }
    });
    state = State::Waiting;
}

void network_tick()
{
    service_serial();
    if (owner == Owner::Serial && lab::capture_ready()) {
        char response[sizeof(tx)];
        if (lab::format_sample(response, sizeof(response), lab::capture_sample())) Serial.print(response);
        else Serial.println("# ERR FORMAT");
        lab::capture_cancel(); owner = Owner::None;
    }
    if (state == State::ConfigRequired || state == State::Fault) {
        return;
    }
    if (link_lost.exchange(false) && state == State::Connected) {
        stop_server();
        state = State::Waiting;
        state_since = millis();
        Serial.printf("[WiFi] link lost; reason=%u, retry in %lu ms\n",
                      disconnect_reason.load(), static_cast<unsigned long>(LAB_WIFI_RETRY_MS));
    }

    const bool online = radio_ready && WiFi.status() == WL_CONNECTED &&
                        static_cast<uint32_t>(WiFi.localIP()) != 0;
    if (online) {
        if (state != State::Connected) {
            state = State::Connected;
            Serial.printf("[WiFi] CONNECTED IP=%s mask=%s gateway=%s RSSI=%ld dBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.subnetMask().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str(), static_cast<long>(WiFi.RSSI()));
            Serial.printf("[LAB] Test from the PC: ping %s\n", WiFi.localIP().toString().c_str());
            start_server();
        } else if (!server_started && millis() - server_retry_since >= 10000UL) {
            start_server();
        }
        service_tcp();
        return;
    }

    if (state == State::Connected) {
        stop_server();
        state = State::Waiting;
        state_since = millis();
        Serial.println("[WiFi] IP unavailable; waiting to retry.");
    }
    if (state == State::Connecting && millis() - state_since >= LAB_WIFI_CONNECT_TIMEOUT_MS) {
        Serial.printf("[WiFi] connection timeout; status=%u reason=%u, retry in %lu ms\n",
                      static_cast<unsigned>(WiFi.status()), disconnect_reason.load(),
                      static_cast<unsigned long>(LAB_WIFI_RETRY_MS));
        WiFi.disconnect(false, false); // No WIFI_OFF, no erasing saved application settings.
        state = State::Waiting;
        state_since = millis();
    }
    if (state == State::Waiting && (first_attempt || millis() - state_since >= LAB_WIFI_RETRY_MS)) {
        first_attempt = false;
        start_attempt();
    }
}
} // namespace lab
