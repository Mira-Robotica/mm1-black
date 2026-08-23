#include "mm1_log.h"

#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>
#include <cstdarg>
#include <math.h>
#include <string.h>
#include <esp_system.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "firmware_version.h"

#if defined(MM1_BOARD_P4)
#include <SD_MMC.h>
#define SD SD_MMC
#else
#include <SD.h>
#endif

#ifndef MM1_LOG_DIR
#define MM1_LOG_DIR "/logs"
#endif

/*
 * Safe logger:
 *  - setup() only enables a RAM buffer (no SD, no NVS).
 *  - SD writes run on a low-priority task after 15 s.
 *  - never call usedBytes() / flush() / hostByName.
 */

static bool     g_ram;
static bool     g_want_sd;
static volatile bool g_hold;
static volatile bool g_sd_dead;
static bool     g_task_on;
static uint32_t g_file_bytes;
static char     g_path[40];
static char     g_buf[512];
static uint16_t g_len;
static uint32_t g_stat_ms;
static SemaphoreHandle_t g_mu;

static uint8_t  g_vol = 0xFF;
static uint32_t g_shots, g_taps, g_ble_legs, g_ble_acks, g_clicks;
static int      g_bat_pct = -1;
static float    g_bat_v   = NAN;
static float    g_v_min   = NAN;
static float    g_v_max   = NAN;
static bool     g_charging;
static uint32_t g_charge_t0;
static uint32_t g_chg_n;

static void lock(void)
{
    if (g_mu)
        xSemaphoreTake(g_mu, portMAX_DELAY);
}
static void unlock(void)
{
    if (g_mu)
        xSemaphoreGive(g_mu);
}

static const char *rst_name(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_SW:       return "SW";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default:               return "OTHER";
    }
}

static void append_line(const char *kind, const char *detail)
{
    if (!g_ram)
        return;
    char line[192];
    int n = snprintf(line, sizeof(line), "T+%lu.%03u %s",
                     (unsigned long)(millis() / 1000UL),
                     (unsigned)(millis() % 1000UL),
                     kind ? kind : "?");
    if (n < 0)
        n = 0;
    if (detail && detail[0] && n < (int)sizeof(line) - 2) {
        line[n++] = ' ';
        for (const char *p = detail; *p && n < (int)sizeof(line) - 2; p++)
            line[n++] = (*p == '\n' || *p == '\r') ? ' ' : *p;
    }
    line[n++] = '\n';
    lock();
    if (g_len + (uint16_t)n >= sizeof(g_buf)) {
        const uint16_t keep = (uint16_t)(g_len / 2);
        memmove(g_buf, g_buf + (g_len - keep), keep);
        g_len = keep;
    }
    if (g_len + (uint16_t)n < sizeof(g_buf)) {
        memcpy(g_buf + g_len, line, (size_t)n);
        g_len = (uint16_t)(g_len + n);
    }
    unlock();
}

static void emit_stat(void)
{
    char d[160];
    snprintf(d, sizeof(d),
             "up=%lus heap=%lu min=%lu bat=%d v=%.2f vol=%u "
             "ui=%lu btn=%lu shots=%lu ble=%lu/%lu tx_b=%lu chg=%lu",
             (unsigned long)(millis() / 1000UL),
             (unsigned long)ESP.getFreeHeap(),
             (unsigned long)ESP.getMinFreeHeap(),
             g_bat_pct,
             isfinite(g_bat_v) ? (double)g_bat_v : 0.0,
             (unsigned)g_vol,
             (unsigned long)g_clicks, (unsigned long)g_taps,
             (unsigned long)g_shots,
             (unsigned long)g_ble_legs, (unsigned long)g_ble_acks,
             (unsigned long)(g_ble_legs * 17UL),
             (unsigned long)g_chg_n);
    append_line("STAT", d);
}

static void log_sd_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    SD.mkdir(MM1_LOG_DIR);
    if (!g_path[0]) {
        snprintf(g_path, sizeof(g_path), MM1_LOG_DIR "/t%08lu.txt",
                 (unsigned long)millis());
    }
    Serial.printf("[LOG] %s\n", g_path);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(8000));
        if (g_sd_dead || g_hold)
            continue;
        char copy[sizeof(g_buf)];
        uint16_t n = 0;
        lock();
        n = g_len;
        if (n) {
            memcpy(copy, g_buf, n);
            g_len = 0;
        }
        unlock();
        if (!n)
            continue;
        File f = SD.open(g_path, FILE_APPEND);
        if (!f) {
            g_sd_dead = true;
            Serial.println("[LOG] SD off");
            break;
        }
        (void)f.write((const uint8_t *)copy, n);
        f.close();
        g_file_bytes += n;
    }
    g_task_on = false;
    vTaskDelete(nullptr);
}

void mm1_log_begin(bool sd_ok)
{
    g_ram     = true;
    g_want_sd = sd_ok;
    g_hold    = false;
    g_sd_dead = false;
    g_task_on = false;
    g_len     = 0;
    g_path[0] = '\0';
    g_stat_ms = millis();
    if (!g_mu)
        g_mu = xSemaphoreCreateMutex();
    append_line("BOOT", FW_VERSION);
}

void mm1_log_event(const char *kind, const char *fmt, ...)
{
    char detail[140];
    detail[0] = '\0';
    if (fmt && fmt[0]) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(detail, sizeof(detail), fmt, ap);
        va_end(ap);
    }
    if (kind && strcmp(kind, "CLICK") == 0)
        g_clicks++;
    append_line(kind, detail);
}

void mm1_log_fail(const char *fmt, ...)
{
    char detail[140];
    detail[0] = '\0';
    if (fmt && fmt[0]) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(detail, sizeof(detail), fmt, ap);
        va_end(ap);
    }
    append_line("FAIL", detail);
}

void mm1_log_battery(int pct, float volts)
{
    g_bat_pct = pct;
    g_bat_v   = volts;
    if (!isfinite(volts) || volts < 2.4f || volts > 4.7f)
        return;
    if (!isfinite(g_v_min) || volts < g_v_min)
        g_v_min = volts;
    if (!isfinite(g_v_max) || volts > g_v_max)
        g_v_max = volts;
    if (!g_charging && volts >= g_v_min + 0.15f) {
        g_charging = true;
        g_charge_t0 = millis();
        g_v_max = volts;
        mm1_log_event("CHARGE", "start v=%.2f pct=%d", (double)volts, pct);
    } else if (g_charging && volts <= g_v_max - 0.10f &&
               (millis() - g_charge_t0) > 180000UL) {
        g_charging = false;
        if (g_v_max - g_v_min >= 0.20f)
            g_chg_n++;
        mm1_log_event("CHARGE", "end v=%.2f loops=%lu",
                      (double)volts, (unsigned long)g_chg_n);
        g_v_min = g_v_max = volts;
    }
}

void mm1_log_feed(uint8_t vol_pct, uint32_t shots, uint32_t btn_taps,
                  uint32_t ble_legs, uint32_t ble_acks)
{
    g_vol = vol_pct;
    g_shots = shots;
    g_taps = btn_taps;
    g_ble_legs = ble_legs;
    g_ble_acks = ble_acks;
}

void mm1_log_hold(bool hold) { g_hold = hold; }

void mm1_log_poll(void)
{
    if (!g_ram)
        return;
    if (g_want_sd && !g_task_on && !g_sd_dead && millis() > 15000UL) {
        g_task_on = true;
        append_line("BOOT", rst_name());
        emit_stat();
        if (xTaskCreate(log_sd_task, "mm1log", 3072, nullptr, 0, nullptr) != pdPASS) {
            g_task_on = false;
            g_sd_dead = true;
        }
    }
    if (millis() - g_stat_ms >= 60000UL) {
        g_stat_ms = millis();
        emit_stat();
    }
}

uint32_t mm1_log_boot_n(void) { return 0; }
const char *mm1_log_path(void) { return g_path; }
bool mm1_log_ready(void) { return g_ram && g_task_on && !g_sd_dead; }

#endif
