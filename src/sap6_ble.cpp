/**
 * SAP6 / CaveBLE GATT server for TopoDroid & SexyTopo.
 * Protocol: https://github.com/furbrain/CircuitPython_CaveBLE
 */

#include "sap6_ble.h"

#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <cstdio>
#include <cstring>

#if defined(MM1_BOARD_P4)
#include "esp32-hal-hosted.h"
#else
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp32-hal-bt.h>
#include <esp_gap_ble_api.h>
#endif

#ifndef SAP6_BLE_DEVICE_NAME
#define SAP6_BLE_DEVICE_NAME "SAP6_0001"
#endif

static const char *kSvcUuid     = "137c4435-8a64-4bcb-93f1-3792c6bdc965";
static const char *kNameUuid    = "137c4435-8a64-4bcb-93f1-3792c6bdc966";
static const char *kCmdUuid     = "137c4435-8a64-4bcb-93f1-3792c6bdc967";
static const char *kLegUuid     = "137c4435-8a64-4bcb-93f1-3792c6bdc968";

static constexpr uint8_t kAck0 = 0x55;
static constexpr uint8_t kAck1 = 0x56;

static constexpr size_t kQueueMax = 32;
static constexpr uint32_t kResendMs = 5000UL;

struct Sap6QueuedLeg {
    float az, inc, roll, dist;
};

static BLEServer *g_server = nullptr;
static BLECharacteristic *g_leg_char = nullptr;
static bool g_stack_ready = false;
static bool g_c6_ready = false;
static bool g_connected = false;

static Sap6QueuedLeg g_queue[kQueueMax];
static size_t g_q_head = 0;
static size_t g_q_tail = 0;
static size_t g_q_count = 0;

static uint8_t g_seq_bit = 0;
static bool g_waiting_ack = false;
static uint32_t g_last_send_ms = 0;
static uint8_t g_last_leg_pkt[17];

static uint32_t g_legs_sent = 0;
static uint32_t g_acks_ok = 0;
static uint32_t g_acks_wrong = 0;
static uint32_t g_resends = 0;
static uint32_t g_adv_kick_ms = 0;
static char g_adv_name[21] = SAP6_BLE_DEVICE_NAME;

static int g_stream_i = 0;
static int g_stream_n = 0;
static const void *g_stream_pts = nullptr;
static size_t g_stream_stride = 0;
static float (*g_stream_az)(const void *) = nullptr;
static float (*g_stream_inc)(const void *) = nullptr;
static float (*g_stream_roll)(const void *) = nullptr;
static float (*g_stream_dist)(const void *) = nullptr;

extern void sap6_on_command(uint8_t cmd);

static void sap6_ble_configure_advertising(const char *device_name);
static void sap6_ble_radio_quiet(void);

static bool queue_push(float az, float inc, float roll, float dist)
{
    if (g_q_count >= kQueueMax)
        return false;
    g_queue[g_q_tail] = { az, inc, roll, dist };
    g_q_tail = (g_q_tail + 1) % kQueueMax;
    g_q_count++;
    return true;
}

static bool queue_pop(Sap6QueuedLeg &out)
{
    if (g_q_count == 0)
        return false;
    out = g_queue[g_q_head];
    g_q_head = (g_q_head + 1) % kQueueMax;
    g_q_count--;
    return true;
}

static void build_leg_packet(uint8_t seq, float az, float inc, float roll, float dist)
{
    memcpy(g_last_leg_pkt, &seq, 1);
    memcpy(g_last_leg_pkt + 1, &az, 4);
    memcpy(g_last_leg_pkt + 5, &inc, 4);
    memcpy(g_last_leg_pkt + 9, &roll, 4);
    memcpy(g_last_leg_pkt + 13, &dist, 4);
}

static void notify_leg_packet(void)
{
    if (!g_leg_char || !g_connected)
        return;
    g_leg_char->setValue(g_last_leg_pkt, sizeof(g_last_leg_pkt));
    g_leg_char->notify();
    g_last_send_ms = millis();
    g_legs_sent++;
}

static void send_next_leg_from_queue(void)
{
    Sap6QueuedLeg leg;
    if (!queue_pop(leg))
        return;
    build_leg_packet(g_seq_bit, leg.az, leg.inc, leg.roll, leg.dist);
    g_seq_bit ^= 1;
    notify_leg_packet();
    g_waiting_ack = true;
}

static void handle_command_byte(uint8_t cmd)
{
    if (cmd == 0)
        return;

    if (cmd == kAck0 || cmd == kAck1) {
        if (g_waiting_ack) {
            const uint8_t expected = (g_seq_bit == 0) ? kAck1 : kAck0;
            if (cmd == expected) {
                g_waiting_ack = false;
                g_acks_ok++;
                send_next_leg_from_queue();
                return;
            }
            /* TopoDroid pode enviar o byte alternativo; ignorar bloqueia o STREAM. */
            g_waiting_ack = false;
            g_acks_ok++;
            g_acks_wrong++;
            send_next_leg_from_queue();
            return;
        }
        /* ACK duplicado/tardio — ignorar (nao desync). */
        return;
    }

    sap6_on_command(cmd);
}

class Sap6ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *) override
    {
        g_connected = true;
    }
    void onDisconnect(BLEServer *srv) override
    {
        g_connected = false;
        g_waiting_ack = false;
        sap6_ble_stream_cancel();
        sap6_ble_configure_advertising(g_adv_name);
        (void)srv;
    }
};

class Sap6CmdCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *ch) override
    {
#if defined(MM1_BOARD_P4)
        const String v = ch->getValue();
        const size_t n = (size_t)v.length();
        const char *p = v.c_str();
#else
        const std::string v = ch->getValue();
        const size_t n = v.size();
        const char *p = v.data();
#endif
        for (size_t i = 0; i < n; i++)
            handle_command_byte((uint8_t)p[i]);
    }
};

static void sap6_ble_radio_quiet(void)
{
#if defined(MM1_BOARD_P4)
    /* C6 Hosted keeps Wi-Fi + BLE together — do not stop Wi-Fi here. */
#else
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
#endif
}

static void sap6_ble_configure_advertising(const char *device_name)
{
    if (!device_name || !device_name[0])
        device_name = SAP6_BLE_DEVICE_NAME;
    strncpy(g_adv_name, device_name, sizeof(g_adv_name) - 1);
    g_adv_name[sizeof(g_adv_name) - 1] = '\0';

#if !defined(MM1_BOARD_P4)
    if (!btStarted())
        return;

    esp_ble_gap_set_device_name(g_adv_name);
#else
    if (!BLEDevice::getInitialized())
        return;
#endif

    /*
     * Padrao ESP32 BLE server (visivel no nRF): UUID no ADV, nome no scan response.
     * A biblioteca trata os eventos GAP; nao chamar esp_ble_gap_config_adv_data() a mao.
     */
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(kSvcUuid);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    adv->setMinInterval(0x20);
    adv->setMaxInterval(0x40);
    BLEDevice::startAdvertising();
    g_adv_kick_ms = millis();
}

static bool sap6_ble_create_gatt_server(void)
{
    if (g_server)
        return true;

    g_server = BLEDevice::createServer();
    if (!g_server)
        return false;
    g_server->setCallbacks(new Sap6ServerCallbacks());

    BLEService *svc = g_server->createService(kSvcUuid);
    if (!svc)
        return false;

    BLECharacteristic *name_ch = svc->createCharacteristic(
        kNameUuid, BLECharacteristic::PROPERTY_READ);
    name_ch->setValue("SAP6");

    BLECharacteristic *cmd_ch = svc->createCharacteristic(
        kCmdUuid, BLECharacteristic::PROPERTY_WRITE);
    cmd_ch->setCallbacks(new Sap6CmdCallbacks());

    g_leg_char = svc->createCharacteristic(
        kLegUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    g_leg_char->addDescriptor(new BLE2902());

    svc->start();
    return true;
}

void sap6_ble_begin(const char *device_name)
{
    if (!device_name || !device_name[0])
        device_name = SAP6_BLE_DEVICE_NAME;

#if defined(MM1_BOARD_P4)
    if (g_stack_ready && g_server && BLEDevice::getInitialized()) {
#else
    if (g_stack_ready && g_server && btStarted()) {
#endif
        sap6_ble_configure_advertising(device_name);
        return;
    }

    g_stack_ready = false;
    g_connected = false;
    g_waiting_ack = false;

    sap6_ble_radio_quiet();

#if defined(MM1_BOARD_P4)
    /* Waveshare 4.3: C6 SDIO CLK18 CMD19 D0=14 D1=15 D2=16 D3=17 RST=54. */
    hostedSetPins(18, 19, 14, 15, 16, 17, 54);
    if (!BLEDevice::getInitialized()) {
        BLEDevice::init(device_name);
    }
    if (!BLEDevice::getInitialized()) {
        return;
    }
#else
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    }

    if (!btStarted() && !btStart()) {
        return;
    }

    if (!BLEDevice::getInitialized()) {
        BLEDevice::init(device_name);
    } else {
        esp_ble_gap_set_device_name(device_name);
    }

    if (!btStarted()) {
        return;
    }

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
#endif

    if (!sap6_ble_create_gatt_server()) {
        return;
    }

    sap6_ble_configure_advertising(device_name);
#if defined(MM1_BOARD_P4)
    g_stack_ready = BLEDevice::getInitialized();
    sap6_ble_c6_refresh();
#else
    g_stack_ready = btStarted();
    g_c6_ready = true;
#endif
}

void sap6_ble_restart(const char *device_name)
{
    g_connected = false;
    g_waiting_ack = false;
    g_stack_ready = false;

    sap6_ble_radio_quiet();

    if (BLEDevice::getInitialized()) {
        BLEDevice::stopAdvertising();
        BLEDevice::deinit(true);
#if !defined(MM1_BOARD_P4)
    } else if (btStarted()) {
        btStop();
#endif
    }

    g_server = nullptr;
    g_leg_char = nullptr;

    vTaskDelay(pdMS_TO_TICKS(200));
    sap6_ble_begin(device_name);
}

static void sap6_ble_stream_tick(void)
{
    if (g_stream_n <= 0 || !g_connected || !g_stream_pts)
        return;

    if (g_stream_i >= g_stream_n)
        return;

    /* Um leg de cada vez (estado ~33 pts OK; fila cheia travava o ESP). */
    if (g_waiting_ack || g_q_count > 0)
        return;

    const uint8_t *p = (const uint8_t *)g_stream_pts + (size_t)g_stream_i * g_stream_stride;
    if (!queue_push(g_stream_az(p), g_stream_inc(p), g_stream_roll(p), g_stream_dist(p)))
        return;
    g_stream_i++;
    send_next_leg_from_queue();
}

void sap6_ble_poll(void)
{
    if (!g_stack_ready)
        return;

    sap6_ble_stream_tick();

    if (!g_connected)
        return;

    if (g_waiting_ack && (millis() - g_last_send_ms) >= kResendMs) {
        notify_leg_packet();
        g_resends++;
    } else if (!g_waiting_ack && g_q_count > 0) {
        send_next_leg_from_queue();
    }
}

bool sap6_ble_stack_ready(void)
{
#if defined(MM1_BOARD_P4)
    return g_stack_ready && BLEDevice::getInitialized();
#else
    return g_stack_ready && btStarted();
#endif
}

bool sap6_ble_c6_ready(void)
{
#if defined(MM1_BOARD_P4)
    return g_c6_ready;
#else
    return true;
#endif
}

#if defined(MM1_BOARD_P4)
static bool sap6_ble_mac_alive(void)
{
    if (!BLEDevice::getInitialized())
        return false;
    const String a = BLEDevice::getAddress().toString();
    unsigned b[6] = {};
    if (sscanf(a.c_str(), "%x:%x:%x:%x:%x:%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return false;
    return (b[0] | b[1] | b[2] | b[3] | b[4] | b[5]) != 0;
}
#endif

bool sap6_ble_c6_refresh(void)
{
#if defined(MM1_BOARD_P4)
    uint32_t maj = 0, min = 0, pat = 0;
    const bool hosted = hostedIsInitialized();
    if (hosted)
        hostedGetSlaveVersion(&maj, &min, &pat);
    const bool fw_ok = (maj | min | pat) != 0;
    const bool mac_ok = sap6_ble_mac_alive();
    /* Version RPC often stays 0.0.0 even when the C6 BLE link is up.
     * A real BLE MAC is enough to allow SoftAP; both-zero still blocks
     * WiFi.mode() (that path resets the P4 when the slave is dead). */
    g_c6_ready = fw_ok || mac_ok;
    Serial.printf("[C6] ready=%d hosted=%d fw=%u.%u.%u ble_mac=%d\n",
                  (int)g_c6_ready, (int)hosted,
                  (unsigned)maj, (unsigned)min, (unsigned)pat, (int)mac_ok);
    return g_c6_ready;
#else
    g_c6_ready = true;
    return true;
#endif
}
bool sap6_ble_connected(void) { return g_connected; }

static bool sap6_format_mac6(char *buf, size_t len, const unsigned *b)
{
    if (!buf || len < 18 || !b)
        return false;
    if ((b[0] | b[1] | b[2] | b[3] | b[4] | b[5]) == 0)
        return false;
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             b[0], b[1], b[2], b[3], b[4], b[5]);
    return true;
}

void sap6_ble_get_mac_str(char *buf, size_t len)
{
    if (!buf || len < 18)
        return;
    buf[0] = '\0';

    /* Phone sees the BLE advertiser address — not WiFi.macAddress()
     * (that is 00:00:00:00:00:00 on P4 until the C6 Hosted link is up). */
    if (BLEDevice::getInitialized()) {
        const String a = BLEDevice::getAddress().toString();
        unsigned b[6] = {};
        if (sscanf(a.c_str(), "%x:%x:%x:%x:%x:%x",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6 &&
            sap6_format_mac6(buf, len, b))
            return;
    }

#if defined(MM1_BOARD_P4)
    snprintf(buf, len, "C6 offline");
#else
    const uint64_t mac = ESP.getEfuseMac();
    const unsigned b[6] = {
        (unsigned)((mac >> 40) & 0xff), (unsigned)((mac >> 32) & 0xff),
        (unsigned)((mac >> 24) & 0xff), (unsigned)((mac >> 16) & 0xff),
        (unsigned)((mac >> 8) & 0xff),  (unsigned)(mac & 0xff),
    };
    if (!sap6_format_mac6(buf, len, b))
        snprintf(buf, len, "-");
#endif
}

void sap6_ble_format_status(char *buf, size_t len)
{
    if (!buf || len < 8)
        return;
#if defined(MM1_BOARD_P4)
    uint32_t maj = 0, min = 0, pat = 0;
    if (hostedIsInitialized())
        hostedGetSlaveVersion(&maj, &min, &pat);
    snprintf(buf, len, "C6 %s  BLE %s  fw %u.%u.%u  heap %u",
             hostedIsInitialized() ? "OK" : "NO",
             g_stack_ready ? "OK" : "OFF",
             (unsigned)maj, (unsigned)min, (unsigned)pat,
             (unsigned)ESP.getFreeHeap());
#else
    snprintf(buf, len, "BT ctrl %d  stack %s  heap %u",
             (int)esp_bt_controller_get_status(),
             (g_stack_ready && btStarted()) ? "OK" : "OFF",
             (unsigned)ESP.getFreeHeap());
#endif
}

bool sap6_ble_try_send_leg(float azimuth_deg, float inclination_deg, float roll_deg,
                           float distance_m)
{
    if (!g_connected || g_waiting_ack || g_q_count > 0)
        return false;
    if (!queue_push(azimuth_deg, inclination_deg, roll_deg, distance_m))
        return false;
    send_next_leg_from_queue();
    return true;
}

void sap6_ble_send_leg(float azimuth_deg, float inclination_deg, float roll_deg,
                       float distance_m)
{
    (void)sap6_ble_try_send_leg(azimuth_deg, inclination_deg, roll_deg, distance_m);
}

bool sap6_ble_waiting_ack(void) { return g_waiting_ack; }

bool sap6_ble_stream_start(int count, const void *pts, size_t pt_stride,
                           float (*get_az)(const void *), float (*get_inc)(const void *),
                           float (*get_roll)(const void *), float (*get_dist)(const void *))
{
    if (count <= 0 || !pts || !get_az || !get_inc || !get_roll || !get_dist)
        return false;
    if (!g_connected || g_stream_n > 0)
        return false;

    g_stream_pts = pts;
    g_stream_stride = pt_stride;
    g_stream_az = get_az;
    g_stream_inc = get_inc;
    g_stream_roll = get_roll;
    g_stream_dist = get_dist;
    g_stream_i = 0;
    g_stream_n = count;
    return true;
}

void sap6_ble_queue_reset(void)
{
    g_q_head = g_q_tail = g_q_count = 0;
    g_waiting_ack = false;
}

void sap6_ble_ack_stall_recover(void)
{
    if (!g_waiting_ack)
        return;
    /* STREAM 1-a-1: nunca saltar para a fila — reenvia o leg em voo (CaveBLE 5s). */
    if (g_q_count > 0)
        g_q_head = g_q_tail = g_q_count = 0;
    if (g_last_leg_pkt[0] <= 1)
        notify_leg_packet();
}

void sap6_ble_stream_cancel(void)
{
    g_stream_n = 0;
    g_stream_i = 0;
    g_stream_pts = nullptr;
    sap6_ble_queue_reset();
}

bool sap6_ble_stream_active(void)
{
    return g_stream_n > 0;
}

void sap6_ble_stream_progress(int *queued_idx, int *total)
{
    if (queued_idx)
        *queued_idx = g_stream_i;
    if (total)
        *total = g_stream_n;
}

void sap6_ble_clear_bonds(void)
{
#if defined(MM1_BOARD_P4)
    /* NimBLE on Hosted starts with bonding off; nothing to wipe. */
#else
    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num <= 0)
        return;
    esp_ble_bond_dev_t list[8];
    if (dev_num > 8)
        dev_num = 8;
    if (esp_ble_get_bond_device_list(&dev_num, list) == ESP_OK) {
        for (int i = 0; i < dev_num; i++)
            esp_ble_remove_bond_device(list[i].bd_addr);
    }
#endif
}

uint32_t sap6_ble_legs_sent(void) { return g_legs_sent; }
uint32_t sap6_ble_acks_ok(void) { return g_acks_ok; }
uint32_t sap6_ble_acks_wrong(void) { return g_acks_wrong; }
uint32_t sap6_ble_resends(void) { return g_resends; }
uint32_t sap6_ble_queue_depth(void) { return (uint32_t)g_q_count + (g_waiting_ack ? 1u : 0u); }

#endif
