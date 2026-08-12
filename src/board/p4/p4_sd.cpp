/**
 * @file p4_sd.cpp
 * @brief SD_MMC bring-up — slot 0 IOMUX pins + on-chip LDO channel 4.
 *
 * Board flags (boards/mm1_p4.json) pre-wire the Arduino SD_MMC constructor:
 * BOARD_HAS_SDMMC, BOARD_SDMMC_SLOT=0, BOARD_SDMMC_POWER_CHANNEL=4.
 * Pins match Waveshare BSP / esp32p4 sdmmc_pins.h (CLK43 CMD44 D0..D3 39..42).
 */

#include "p4_sd.h"

#include <SD_MMC.h>

#include "mm1_p4_pins.h"

namespace {
bool g_ready = false;
}

bool p4_sd_begin()
{
    /* Constructor already selected IOMUX slot 0 + LDO ch4 via board macros.
     * setPins is a no-op validation that the requested pins match IOMUX. */
    if (!SD_MMC.setPins(MM1_SD_CLK, MM1_SD_CMD, MM1_SD_D0, MM1_SD_D1, MM1_SD_D2, MM1_SD_D3)) {
        Serial.println("p4_sd: setPins rejected (expected IOMUX slot 0)");
        g_ready = false;
        return false;
    }
#ifdef SOC_SDMMC_IO_POWER_EXTERNAL
    if (!SD_MMC.setPowerChannel(MM1_SD_LDO_CHAN)) {
        Serial.println("p4_sd: setPowerChannel failed");
        g_ready = false;
        return false;
    }
#endif
    /* Mount at /sdcard; 4-bit; do not format on fail. */
    g_ready = SD_MMC.begin("/sdcard", false, false);
    if (!g_ready) {
        Serial.println("p4_sd: mount failed — insert a FAT32 card and retry");
        return false;
    }
    Serial.printf("p4_sd: OK type=%d size=%llu MB used=%llu MB\n",
                  (int)SD_MMC.cardType(),
                  (unsigned long long)(SD_MMC.cardSize() / (1024ULL * 1024ULL)),
                  (unsigned long long)(SD_MMC.usedBytes() / (1024ULL * 1024ULL)));
    return true;
}

bool p4_sd_ready()
{
    return g_ready;
}

uint64_t p4_sd_total_mb()
{
    return g_ready ? SD_MMC.cardSize() / (1024ULL * 1024ULL) : 0;
}

uint64_t p4_sd_used_mb()
{
    return g_ready ? SD_MMC.usedBytes() / (1024ULL * 1024ULL) : 0;
}

String p4_sd_status_line()
{
    if (!g_ready) {
        return String("SD: not mounted");
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "SD: %llu/%llu MB",
             (unsigned long long)p4_sd_used_mb(),
             (unsigned long long)p4_sd_total_mb());
    return String(buf);
}
