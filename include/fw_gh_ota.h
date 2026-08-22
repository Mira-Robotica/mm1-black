#pragma once

#ifdef ARDUINO_ARCH_ESP32

/** GitHub Pages manifest + bin (same site as the USB installer). */
#ifndef FW_GH_MANIFEST_URL
#define FW_GH_MANIFEST_URL "https://verlab.github.io/mm1-black/latest.json"
#endif
#ifndef FW_GH_BASE_URL
#define FW_GH_BASE_URL "https://verlab.github.io/mm1-black/"
#endif

#if defined(MM1_BOARD_P4)
#define FW_GH_BOARD_KEY "mm1_p4"
#else
#define FW_GH_BOARD_KEY "denky32"
#endif

void fw_gh_ota_request_check(void);
void fw_gh_ota_request_install(void);
void fw_gh_ota_poll(void);

bool fw_gh_ota_busy(void);
bool fw_gh_ota_newer(void);
int  fw_gh_ota_percent(void);
const char *fw_gh_ota_status(void);
const char *fw_gh_ota_latest_tag(void);

/** Compare FW_VERSION vs tag (vX.Y.Z). +1 if remote is newer. */
int fw_gh_ver_cmp(const char *remote_tag, const char *local);

#endif
