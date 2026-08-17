#include "p4_btn.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "mm1_p4_pins.h"

namespace {
int g_pressed_level = LOW;
}

void p4_btn_init(void)
{
    const int pin = MM1_USER_BUTTON;
    gpio_reset_pin((gpio_num_t)pin);
    gpio_hold_dis((gpio_num_t)pin);

    /* Same as v0.7.2: INPUT_PULLUP, closed switch → GND → LOW.
     * If the 4-pin is wired high-side (3V3→NO, C→GPIO), idle stays HIGH
     * with pull-up — switch to pull-down so press reads HIGH. */
    pinMode(pin, INPUT_PULLUP);
    delay(8);
    const int with_up = digitalRead(pin);

    pinMode(pin, INPUT_PULLDOWN);
    delay(8);
    const int with_dn = digitalRead(pin);

    if (with_up == HIGH && with_dn == HIGH) {
        pinMode(pin, INPUT_PULLDOWN);
        g_pressed_level = HIGH;
        Serial.printf("[BTN] GPIO%d high-side (3V3→NO C→GPIO) press=1 idle=%d\n",
                      pin, digitalRead(pin));
    } else {
        pinMode(pin, INPUT_PULLUP);
        g_pressed_level = LOW;
        Serial.printf("[BTN] GPIO%d C→GND NO→GPIO press=0 idle=%d\n",
                      pin, digitalRead(pin));
    }
}

int p4_btn_level(void)
{
    return digitalRead(MM1_USER_BUTTON);
}

bool p4_btn_pressed(void)
{
    return digitalRead(MM1_USER_BUTTON) == g_pressed_level;
}
