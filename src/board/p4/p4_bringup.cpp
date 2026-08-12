/**
 * @file p4_bringup.cpp
 * @brief ESP32-P4 bring-up: panel + GT911 + LVGL 8.3 smoke test.
 *
 * Replaces the colour-sweep / raw-touch probes once those paths were confirmed.
 * Next step after this demo is wiring the MM1 UI (still in src/main.cpp on CYD).
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>

#include "mm1_p4_pins.h"
#include "p4_lvgl.h"
#include "p4_sd.h"

using namespace esp_panel::board;

namespace {

Board *g_board = nullptr;
lv_obj_t *g_lbl = nullptr;
lv_obj_t *g_sd_lbl = nullptr;
int g_taps = 0;

void on_btn(lv_event_t *e)
{
    (void)e;
    g_taps++;
    if (g_lbl) {
        lv_label_set_text_fmt(g_lbl, "taps: %d", g_taps);
    }
}

void build_demo_ui()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a1a), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "MM1-BLACK  P4");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "LVGL 8.3 + ST7701 + GT911");
    lv_obj_set_style_text_color(sub, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 220, 80);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, on_btn, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "TAP");
    lv_obj_center(btn_lbl);

    g_lbl = lv_label_create(scr);
    lv_label_set_text(g_lbl, "taps: 0");
    lv_obj_set_style_text_color(g_lbl, lv_color_hex(0x00e676), 0);
    lv_obj_align(g_lbl, LV_ALIGN_BOTTOM_MID, 0, -80);

    g_sd_lbl = lv_label_create(scr);
    lv_label_set_text(g_sd_lbl, p4_sd_status_line().c_str());
    lv_obj_set_style_text_color(g_sd_lbl,
                                lv_color_hex(p4_sd_ready() ? 0x80cbc4 : 0xff8a80), 0);
    lv_obj_align(g_sd_lbl, LV_ALIGN_BOTTOM_MID, 0, -40);
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("MM1-BLACK — ESP32-P4 LVGL bring-up");
    Serial.printf("chip: %s, PSRAM: %u KB, heap: %u KB\n",
                  ESP.getChipModel(),
                  (unsigned)(ESP.getPsramSize() / 1024),
                  (unsigned)(ESP.getFreeHeap() / 1024));

    if (MM1_LCD_BL_EN >= 0) {
        pinMode(MM1_LCD_BL_EN, OUTPUT);
        digitalWrite(MM1_LCD_BL_EN, HIGH);
    }

    g_board = new Board();
    if (!g_board->init() || !g_board->begin()) {
        Serial.println("board init/begin failed");
        return;
    }
    if (auto backlight = g_board->getBacklight()) {
        backlight->setBrightness(80);
    }

    p4_sd_begin();

    if (!p4_lvgl_init(g_board)) {
        Serial.println("lvgl init failed");
        return;
    }
    build_demo_ui();
    Serial.println("LVGL demo ready — tap the button");
}

void loop()
{
    p4_lvgl_handler();
    delay(5);
}
