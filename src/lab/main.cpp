#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_arduino_version.h>

#include "board/p4/mm1_p4_pins.h"
#include "firmware_version.h"
#include "network.h"

#if !defined(MM1_BOARD_P4) || !defined(MM1_LAB)
#error "Build this entry point with env:mm1_p4_lab."
#endif

void setup()
{
    // Keep the backlight and speaker disabled without initializing panel/I2C/I2S.
    for (const int pin : {MM1_LCD_BL_EN, MM1_LCD_BL_PWM, MM1_AMP_EN}) {
        gpio_set_level(static_cast<gpio_num_t>(pin), 0);
        gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
    }

    Serial.begin(115200);
    delay(300);
    Serial.printf("\n[LAB] MM1-P4 network bring-up | firmware=%s\n", FW_VERSION);
    Serial.printf("[LAB] chip=%s revision=%u cpu=%lu MHz Arduino=%s IDF=%s\n",
                  ESP.getChipModel(), ESP.getChipRevision(),
                  static_cast<unsigned long>(ESP.getCpuFreqMHz()),
                  ESP_ARDUINO_VERSION_STR, ESP.getSdkVersion());
    Serial.println("[LAB] Display/audio disabled. Sensors are not initialized in increment 1.");
    lab::network_begin();
}

void loop()
{
    lab::network_tick();
    delay(5);
}
