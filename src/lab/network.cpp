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

#if __has_include("lab_config.h")
#include "lab_config.h"
#else
#include "lab_config.example.h"
#endif

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
char tx[640]{};
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
    client.stop();
    tcp_line.reset();
    tx_size = tx_sent = 0;
}

void stop_server()
{
    close_client();
    server.end();
    server_started = false;
}

void queue_reply(const char *reply)
{
    // Read the next command only after the previous reply has been sent.
    const size_t len = strlen(reply);
    if (len >= sizeof(tx) || tx_sent != tx_size) {
        close_client();
        return;
    }
    memcpy(tx, reply, len);
    tx_size = len;
    tx_sent = 0;
    tx_since = millis();
}

void format_status(char *out, size_t size)
{
    const bool online = state == State::Connected;
    const String ip = online ? WiFi.localIP().toString() : String("0.0.0.0");
    snprintf(out, size,
             "# OK STATUS stage=network_only wifi=%s ip=%s rssi_dbm=%ld "
             "tcp=%s port=%u hosted=%u sensors=NOT_IMPLEMENTED "
             "attempt=%lu disconnect_reason=%u uptime_ms=%lu\n",
             state_name(), ip.c_str(), online ? static_cast<long>(WiFi.RSSI()) : 0L,
             server_started ? "LISTENING" : "OFF", static_cast<unsigned>(LAB_TCP_PORT),
             static_cast<unsigned>(hostedIsInitialized()),
             static_cast<unsigned long>(attempt), disconnect_reason.load(),
             static_cast<unsigned long>(millis()));
}

void command(const char *line, bool from_serial)
{
    char reply[sizeof(tx)];
    if (strcmp(line, "STATUS") == 0) {
        format_status(reply, sizeof(reply));
    } else if (strcmp(line, "CAPTURE") == 0 || strncmp(line, "CAPTURE ", 8) == 0) {
        snprintf(reply, sizeof(reply), "# ERR NOT_IMPLEMENTED stage=network_only\n");
    } else {
        snprintf(reply, sizeof(reply), "# ERR BAD_COMMAND supported=STATUS\n");
    }
    if (from_serial) {
        Serial.print(reply);
    } else {
        queue_reply(reply);
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
            char hello[192];
            snprintf(hello, sizeof(hello),
                     "# HELLO MM1LAB 2 stage=network_only boot_id=%s connection_id=%lu\n"
                     "# META capture=unavailable commands=STATUS\n",
                     boot_id, static_cast<unsigned long>(connection_id));
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
    if (tx_sent < tx_size) {
        // NetworkClient::write can retry/block. Send a small reply without waiting.
        const int sent = ::send(client.fd(), tx + tx_sent, tx_size - tx_sent, MSG_DONTWAIT);
        if (sent > 0) {
            tx_sent += static_cast<size_t>(sent);
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            close_client();
        }
        if (tx_sent < tx_size && millis() - tx_since >= 2000UL) {
            close_client();
        }
        return;
    }
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

void start_server()
{
    server_retry_since = millis();
    server.begin();
    server_started = static_cast<bool>(server);
    Serial.printf("[LAB] TCP port=%u %s\n", static_cast<unsigned>(LAB_TCP_PORT),
                  server_started ? "LISTENING (STATUS)" : "FAILED; retry in 10 s");
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
