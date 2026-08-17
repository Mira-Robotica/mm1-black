# ESP32-P4 port (v1.0.0 target)

MM1-BLACK v0.x runs on the ESP32 "CYD" board (ST7796 SPI + XPT2046). v1.0.0 moves
to the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3**. This is a platform change, not a
board swap: the SoC architecture, the display bus, the touch controller, the SD bus
and the radio all change at once.

Both targets live in the same tree while the port is in progress:

| Env | Board | Status |
|-----|-------|--------|
| `denky32` | ESP32 CYD 4" (ST7796 + XPT2046) | Production, v0.7.2 |
| `mm1_p4` | Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 | Bring-up |

```bash
pio run -e denky32     # CYD
pio run -e mm1_p4      # ESP32-P4
```

Build them in **separate invocations**. `pio run -e denky32 -e mm1_p4` fails on the
second environment: both platforms claim the same package names, so once one of them
has resolved `framework-arduinoespressif32` in a process, the other cannot find its
own copy and dies with `TypeError: ... not 'NoneType'` in `arduino.py`. CI keeps the
two targets in separate jobs for this reason.

## What the new hardware changes

| Subsystem | CYD | ESP32-P4 board |
|---|---|---|
| SoC | ESP32 Xtensa, 240 MHz, 520 KB SRAM | ESP32-P4 RISC-V dual-core, 400 MHz, 32 MB PSRAM, 32 MB flash |
| Display | ST7796 480×320 over SPI, TFT_eSPI | ST7701 480×800 over MIPI-DSI 2-lane, `esp_lcd` |
| Touch | XPT2046 resistive, shared SPI | GT911 capacitive 5-point, I²C |
| SD | SPI on HSPI, `SD.h` | SDIO 3.0 4-bit, `SD_MMC` |
| Radio | Wi-Fi + BLE on-die | None on the P4 — ESP32-C6-MINI over SDIO via ESP-Hosted |
| Audio | Buzzer on LEDC + amp enable | ES8311 codec (+ ES7210) over I²S |

The P4 has **no radio of its own**. Wi-Fi 6 and BLE 5 come from the on-board
ESP32-C6-MINI reached over SDIO with ESP-Hosted, which means the C6 needs its own
version-matched slave firmware. Field updates therefore involve two binaries.

## Toolchain

The official PlatformIO `espressif32` platform does not support ESP32-P4, so
`env:mm1_p4` uses the community fork
[pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32)
(Arduino core 3.3.x on ESP-IDF 5.5.x).

**The fork publishes itself under the same platform name (`espressif32`) and the same
package names (`framework-arduinoespressif32`, `tool-esptoolpy`) as the official
platform, and installing it replaces those shared packages in `~/.platformio`.** That
is why `env:denky32` pins both:

```ini
platform = espressif32@6.13.0
platform_packages =
	platformio/tool-esptoolpy@~1.40501.0
```

Without the pins, the CYD build silently jumps to Arduino core 3.x and fails on
`ledcSetup`, `String` → `std::string`, and a missing `intelhex` module in esptool.

`ESP32_Display_Panel` and its dependencies are pinned to git tags because the
PlatformIO registry mirror is stale: it carries a higher version number but still
ships the pre-1.0 `ESP_Panel_Conf.h` API, which has no MIPI-DSI support.

## Board description

`src/board/p4/esp_panel_board_custom_conf.h` describes the panel for
ESP32_Display_Panel. Every value is taken from Waveshare's own BSP for this exact
board ([waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3),
`components/esp32_p4_wifi6_touch_lcd_4_3/`) and the published schematic:

- 2 lanes at 500 Mbps, DPI clock 30 MHz, RGB565
- HPW 12 / HBP 42 / HFP 42, VPW 8 / VBP 2 / VFP 60
- DSI PHY powered from internal LDO channel 3 at 2500 mV
- LCD reset GPIO27, backlight PWM GPIO26, backlight boost enable GPIO33,
  touch reset GPIO23, touch interrupt not connected (test point TP2 only)

**The ST7701 vendor init sequence is mandatory.** `ESP_PANEL_BOARD_LCD_VENDOR_INIT_CMD()`
carries the 39 panel-specific commands from the BSP. Without them the DSI link comes up
without a single error and the panel still stays black, because the controller never
leaves sleep — the sequence ends with `0x11` (sleep out, 120 ms) and `0x29` (display on).
`LCD ID: FF FF FF` in the log is expected on this panel and is not a fault.

The board ships **ECO2 silicon** (`ESP-ROM:esp32p4-eco2`), so `boards/mm1_p4.json` must
use `chip_variant = esp32p4_es` and `f_cpu = 360000000L`. The plain `esp32p4` variant
panics with an illegal instruction inside the bootloader.

## J3 peripheral pin map

Settled in `src/board/p4/mm1_p4_pins.h` (silk labels on the 40-pin header):

| Function | Silk / GPIO | Notes |
|---|---|---|
| IMU SDA / SCL | **30 / 31** | Software I2C. Not silk SDA/SCL (GT911). GPIO28/29 idle LOW on this header. |
| IMU INT | — | Not used (firmware polls) |
| Laser RX / TX | 21 / 22 | Module TX→21, module RX→22; swap if no UART data |
| Capture button | **5** | Silk `5` == GPIO5; wire to adjacent GND (active low) |
| Battery ADC | — | Not exposed on this carrier |

Schematic: `docs/datasheets/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf`.
GPIOs 16–20 are the ESP32-C6 SDIO link and do **not** appear on J3.
Silk **SDA/SCL** are GPIO7/8 (touch/codecs only).

## Port order

1. **Toolchain + panel** — done and verified on hardware. `src/board/p4/p4_bringup.cpp`
   brings up the DSI panel (colour sweep confirmed visually), the shared I²C bus
   (ES8311 at 0x18, ES7210 at 0x40, GT911 at 0x5D) and the backlight.
   **Open:** GT911 answers on I²C and identifies itself, but its status register
   `0x814E` never reports a contact. Note that the host must write 0 back to `0x814E`
   after every read or the controller stops refreshing coordinates, and that the
   library driver polls and clears the same register every 20 ms — the
   `kRawTouchDiag` flag in the bring-up sketch disables one side while debugging.
2. **LVGL 8.3 on the new panel**, buffers in PSRAM. Staying on 8.3 keeps the ~3000
   lines of existing UI code valid; LVGL 9 is a separate migration.
3. **BLE SAP6 over ESP-Hosted.** The GATT logic in `src/sap6_ble.cpp` is portable;
   the controller-level calls (`btStart`, `esp_bt_controller_mem_release`,
   `esp_ble_tx_power_set`, `esp_ble_get_bond_device_*`) are not, because the
   controller lives on the C6.
4. **SD_MMC, BNO086, laser UART, capture button.** The CSV/frozen-prefix logic and
   the laser protocol parser carry over unchanged.
5. **UI for 480×800.** `UI_COMPACT_HEADER` (`SCREEN_W < 400`) inverts meaning, fonts
   need to grow for the higher DPI, and `LV_MEM_SIZE` (48 KB today) is too small.
   The splash in `src/mira_splash_img.c` must be regenerated with
   `tools/gen_mira_splash.py`.
6. **Release pipeline.** `docs/flasher/` is hard-coded to the classic ESP32
   (`BIN_PREFIX`, `FLASH_ADDR = 0x10000`, DTR/RTS reset). The P4 uses a different
   bootloader offset, a virgin board needs bootloader + partition table + app, and
   esptool-js has had reports of corrupted compressed writes on P4 boards.

## What does not carry over

`TFT_eSPI` is SPI-only and has no ESP32-P4 support. It goes away entirely, together
with `include/User_Setup.h`, `board_support/TFT_eSPI_User_Setup.h` and
`scripts/pio_tft_setup_copy.py`.
