/**
 * @file p4_boot.cpp
 * @brief Boot splash (centered CYD asset) + ES8311/I2S chime on P4.
 *
 * Codec control uses the legacy I2C driver already owned by Display_Panel
 * (I2C_NUM_0 on GPIO7/8). Wire/i2c_master must not be linked.
 */

#include "p4_boot.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <driver/i2c.h>
#include <esp_display_panel.hpp>

#include "mm1_p4_pins.h"
#include "p4_board.h"

using namespace esp_panel::drivers;

namespace {

constexpr int kBandRows = 8;
uint16_t g_band[480 * kBandRows];

constexpr int kSampleRate = 16000;
constexpr int kMclkHz     = kSampleRate * 256; /* 4.096 MHz */
constexpr uint8_t kEsAddr = 0x18;              /* Waveshare CE strapped low */

I2SClass g_i2s;
bool g_audio_ok = false;

void fill_rect(LCD *lcd, int x, int y, int w, int h, uint16_t colour)
{
    if (lcd == nullptr || w <= 0 || h <= 0) {
        return;
    }
    const size_t n = static_cast<size_t>(w) * kBandRows;
    for (size_t i = 0; i < n; i++) {
        g_band[i] = colour;
    }
    for (int row = 0; row < h; row += kBandRows) {
        const int bh = std::min(kBandRows, h - row);
        lcd->drawBitmap(x, y + row, w, bh, reinterpret_cast<const uint8_t *>(g_band), 1000);
    }
}

bool es_write(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kEsAddr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

bool es8311_init_dac(void)
{
    /* Soft reset + power-on (Espressif es8311 component sequence). */
    if (!es_write(0x00, 0x1F)) {
        return false;
    }
    delay(20);
    if (!es_write(0x00, 0x00) || !es_write(0x00, 0x80)) {
        return false;
    }

    /* Clocks from MCLK pin; coeff for 4.096 MHz / 16 kHz. */
    if (!es_write(0x01, 0x3F)) {
        return false;
    }
    /* pre_div=1, pre_multi=0 */
    if (!es_write(0x02, 0x00)) {
        return false;
    }
    /* adc_osr / dac_osr */
    if (!es_write(0x03, 0x10) || !es_write(0x04, 0x10)) {
        return false;
    }
    /* adc_div=1, dac_div=1 */
    if (!es_write(0x05, 0x00)) {
        return false;
    }
    /* bclk_div=4 → (4-1)=3 */
    if (!es_write(0x06, 0x03)) {
        return false;
    }
    /* lrck divider 0x00FF */
    if (!es_write(0x07, 0x00) || !es_write(0x08, 0xFF)) {
        return false;
    }

    /* Slave I2S, 16-bit in/out */
    if (!es_write(0x09, 0x0C) || !es_write(0x0A, 0x0C)) {
        return false;
    }

    if (!es_write(0x0D, 0x01) || /* analog power */
        !es_write(0x0E, 0x02) ||
        !es_write(0x12, 0x00) || /* DAC power-up */
        !es_write(0x13, 0x10) || /* HP drive */
        !es_write(0x1C, 0x6A) ||
        !es_write(0x37, 0x08)) {
        return false;
    }

    /* Volume ~80%, unmute */
    if (!es_write(0x32, 0xBF) || !es_write(0x31, 0x00)) {
        return false;
    }
    (void)kMclkHz;
    return true;
}

} // namespace

void p4_boot_show_splash(const uint8_t *rgb565, int img_w, int img_h)
{
    auto board = p4_board_get();
    if (board == nullptr || rgb565 == nullptr || img_w <= 0 || img_h <= 0) {
        return;
    }
    auto lcd = board->getLCD();
    if (lcd == nullptr) {
        return;
    }
    const int scr_w = lcd->getFrameWidth();
    const int scr_h = lcd->getFrameHeight();
    fill_rect(lcd, 0, 0, scr_w, scr_h, 0x0000);

    const int x0 = (scr_w - img_w) / 2;
    const int y0 = (scr_h - img_h) / 2;
    const auto *src = reinterpret_cast<const uint16_t *>(rgb565);

    if (x0 < 0 || y0 < 0) {
        const int dw = std::min(img_w, scr_w);
        const int dh = std::min(img_h, scr_h);
        for (int y = 0; y < dh; y += kBandRows) {
            const int bh = std::min(kBandRows, dh - y);
            for (int row = 0; row < bh; row++) {
                std::memcpy(g_band + row * dw, src + (y + row) * img_w,
                            static_cast<size_t>(dw) * sizeof(uint16_t));
            }
            lcd->drawBitmap(0, y, dw, bh, reinterpret_cast<const uint8_t *>(g_band), 1000);
        }
        return;
    }

    for (int y = 0; y < img_h; y += kBandRows) {
        const int bh = std::min(kBandRows, img_h - y);
        for (int row = 0; row < bh; row++) {
            std::memcpy(g_band + row * img_w, src + (y + row) * img_w,
                        static_cast<size_t>(img_w) * sizeof(uint16_t));
        }
        lcd->drawBitmap(x0, y0 + y, img_w, bh, reinterpret_cast<const uint8_t *>(g_band), 1000);
    }
}

void p4_boot_audio_init(void)
{
    g_audio_ok = false;

    if (MM1_AMP_EN >= 0) {
        pinMode(MM1_AMP_EN, OUTPUT);
        digitalWrite(MM1_AMP_EN, HIGH);
    }

    if (!es8311_init_dac()) {
        Serial.println("p4_boot: ES8311 init failed (no beep)");
        return;
    }

    g_i2s.setPins(MM1_I2S_SCLK, MM1_I2S_LCLK, MM1_I2S_DOUT, -1, MM1_I2S_MCLK);
    if (!g_i2s.begin(I2S_MODE_STD, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        Serial.println("p4_boot: I2S begin failed");
        return;
    }

    g_audio_ok = true;
    Serial.println("p4_boot: ES8311 + I2S OK");
}

void p4_boot_buzzer_note(unsigned freq_hz, unsigned dur_ms)
{
    if (!g_audio_ok) {
        delay(dur_ms);
        return;
    }
    if (dur_ms == 0) {
        return;
    }

    const size_t total_frames = static_cast<size_t>(kSampleRate) * dur_ms / 1000U;
    if (total_frames == 0) {
        return;
    }

    constexpr size_t kChunk = 256;
    int16_t buf[kChunk * 2];
    size_t done = 0;
    const float phase_inc =
        (freq_hz == 0) ? 0.f : (2.f * static_cast<float>(M_PI) * static_cast<float>(freq_hz) /
                                static_cast<float>(kSampleRate));
    float phase = 0.f;

    while (done < total_frames) {
        const size_t n = std::min(kChunk, total_frames - done);
        for (size_t i = 0; i < n; i++) {
            int16_t s = 0;
            if (freq_hz != 0) {
                s = static_cast<int16_t>(sinf(phase) * 12000.f);
                phase += phase_inc;
                if (phase > 2.f * static_cast<float>(M_PI)) {
                    phase -= 2.f * static_cast<float>(M_PI);
                }
            }
            buf[i * 2]     = s;
            buf[i * 2 + 1] = s;
        }
        g_i2s.write(reinterpret_cast<const uint8_t *>(buf), n * 4);
        done += n;
    }
}
