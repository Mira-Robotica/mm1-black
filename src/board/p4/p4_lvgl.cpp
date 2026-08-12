/**
 * @file p4_lvgl.cpp
 * @brief LVGL 8.3 display/touch port for ESP32-P4 (ST7701 MIPI-DSI + GT911).
 *
 * Partial buffers live in PSRAM. Flush goes through LCD::drawBitmap, which the
 * colour-sweep bring-up already proved. Touch uses the panel library's GT911
 * driver (polling; TP_INT is not wired on this board).
 */

#include "p4_lvgl.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {

Board *g_board = nullptr;
LCD *g_lcd = nullptr;
Touch *g_touch = nullptr;

lv_disp_draw_buf_t g_draw_buf;
lv_disp_drv_t g_disp_drv;
lv_indev_drv_t g_indev_drv;
lv_color_t *g_buf1 = nullptr;
lv_color_t *g_buf2 = nullptr;

/* ~40 lines × 480 × 2 B ≈ 38 KB per buffer — comfortable in 32 MB PSRAM. */
constexpr int kBufLines = 40;

void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    if (g_lcd == nullptr) {
        lv_disp_flush_ready(drv);
        return;
    }
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    g_lcd->drawBitmap(area->x1, area->y1, w, h,
                      reinterpret_cast<const uint8_t *>(color_p), 1000);
    lv_disp_flush_ready(drv);
}

void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->state = LV_INDEV_STATE_RELEASED;
    if (g_touch == nullptr) {
        return;
    }
    TouchPoint pts[1];
    const int n = g_touch->readPoints(pts, 1, 0);
    if (n > 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = pts[0].x;
        data->point.y = pts[0].y;
    }
}

} // namespace

bool p4_lvgl_init(Board *board)
{
    if (board == nullptr) {
        return false;
    }
    g_board = board;
    g_lcd = board->getLCD();
    g_touch = board->getTouch();
    if (g_lcd == nullptr) {
        Serial.println("p4_lvgl: no LCD");
        return false;
    }

    const int hor = board->getLcdWidth();
    const int ver = board->getLcdHeight();
    const size_t pix = static_cast<size_t>(hor) * kBufLines;
    const size_t bytes = pix * sizeof(lv_color_t);

    bool in_psram = true;
    g_buf1 = static_cast<lv_color_t *>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_buf2 = static_cast<lv_color_t *>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_buf1 == nullptr || g_buf2 == nullptr) {
        Serial.println("p4_lvgl: PSRAM buffer alloc failed — falling back to SRAM");
        in_psram = false;
        if (g_buf1 == nullptr) {
            g_buf1 = static_cast<lv_color_t *>(malloc(bytes));
        }
        if (g_buf2 == nullptr) {
            g_buf2 = static_cast<lv_color_t *>(malloc(bytes));
        }
    }
    if (g_buf1 == nullptr) {
        Serial.println("p4_lvgl: buffer alloc failed");
        return false;
    }

    lv_init();
    lv_disp_draw_buf_init(&g_draw_buf, g_buf1, g_buf2, pix);

    lv_disp_drv_init(&g_disp_drv);
    g_disp_drv.hor_res = hor;
    g_disp_drv.ver_res = ver;
    g_disp_drv.flush_cb = disp_flush;
    g_disp_drv.draw_buf = &g_draw_buf;
    lv_disp_drv_register(&g_disp_drv);

    if (g_touch != nullptr) {
        lv_indev_drv_init(&g_indev_drv);
        g_indev_drv.type = LV_INDEV_TYPE_POINTER;
        g_indev_drv.read_cb = touch_read;
        lv_indev_drv_register(&g_indev_drv);
    } else {
        Serial.println("p4_lvgl: no touch — display only");
    }

    Serial.printf("p4_lvgl: %dx%d, buf=%u KB x%d in %s\n",
                  hor, ver,
                  (unsigned)(bytes / 1024),
                  g_buf2 ? 2 : 1,
                  in_psram ? "PSRAM" : "SRAM");
    return true;
}

void p4_lvgl_handler()
{
    lv_timer_handler();
}
