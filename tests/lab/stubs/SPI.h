#pragma once
#include "Arduino.h"
constexpr int MSBFIRST=0, SPI_MODE3=3;
struct SPISettings { SPISettings(unsigned long,int,int) {} };
struct SPIClass {
    void begin() {}
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
    void transfer(uint8_t *, size_t) {}
    uint8_t transfer(uint8_t x) { return x; }
    void transferBytes(const uint8_t *, uint8_t *, size_t) {}
};
inline SPIClass SPI;
