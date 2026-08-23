#include "fw_gh_ota.h"

#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <stdlib.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "firmware_version.h"

static void apply_public_dns(void)
{
    const IPAddress d0(8, 8, 8, 8);
    const IPAddress d1(1, 1, 1, 1);
    (void)WiFi.setDNS(d0, d1);

    ip_addr_t a0, a1;
    IP_ADDR4(&a0, 8, 8, 8, 8);
    IP_ADDR4(&a1, 1, 1, 1, 1);
    dns_setserver(0, &a0);
    dns_setserver(1, &a1);

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_dns_info_t info = {};
        info.ip.type = ESP_IPADDR_TYPE_V4;
        info.ip.u_addr.ip4.addr = ESP_IP4TOADDR(8, 8, 8, 8);
        (void)esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &info);
        info.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
        (void)esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &info);
    }
}

void fw_gh_ota_bind_sta(void)
{
    if (WiFi.status() != WL_CONNECTED || WiFi.localIP()[0] == 0)
        return;
    apply_public_dns();
    Serial.printf("[WiFi] bind  IP %s  GW %s  DNS %s\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(),
                  WiFi.dnsIP().toString().c_str());
}

void fw_gh_ota_mark_boot_ok(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (!p)
        return;
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(p, &st) != ESP_OK)
        return;
    if (st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            Serial.println("[OTA] boot confirmed");
    }
}

namespace {

enum Job : uint8_t { JobNone = 0, JobCheck, JobInstall };

Job  g_job = JobNone;
bool g_busy = false;
bool g_newer = false;
int  g_pct = 0;
char g_status[80] = "idle";
char g_latest[24] = "";
char g_rel_file[96] = "";

int parse_trip(const char *s, int *maj, int *min, int *pat)
{
    if (!s || !s[0])
        return 0;
    if (s[0] == 'v' || s[0] == 'V')
        s++;
    char *end = nullptr;
    long a = strtol(s, &end, 10);
    if (!end || *end != '.')
        return 0;
    long b = strtol(end + 1, &end, 10);
    if (!end || *end != '.')
        return 0;
    long c = strtol(end + 1, &end, 10);
    *maj = (int)a;
    *min = (int)b;
    *pat = (int)c;
    return 1;
}

bool json_str_after(const char *json, const char *anchor, const char *key,
                    char *out, size_t outsz)
{
    if (!json || !key || !out || outsz < 2)
        return false;
    const char *p = json;
    if (anchor && anchor[0]) {
        p = strstr(json, anchor);
        if (!p)
            return false;
    }
    const char *k = strstr(p, key);
    if (!k)
        return false;
    k = strchr(k, ':');
    if (!k)
        return false;
    k++;
    while (*k == ' ' || *k == '\t')
        k++;
    if (*k != '"')
        return false;
    k++;
    size_t n = 0;
    while (k[n] && k[n] != '"' && n + 1 < outsz)
        n++;
    if (!n)
        return false;
    memcpy(out, k, n);
    out[n] = '\0';
    return true;
}

int  g_http_code = 0;

void http_status_from_code(int code)
{
    snprintf(g_status, sizeof(g_status), "Could not reach updates");
    Serial.printf("[OTA] HTTP %d\n", code);
}

bool sta_ready(void)
{
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(g_status, sizeof(g_status), "Join a network first");
        return false;
    }
    if (WiFi.localIP()[0] == 0) {
        snprintf(g_status, sizeof(g_status), "Still connecting…");
        return false;
    }
    apply_public_dns();
    return true;
}

void secure_prep(WiFiClientSecure &cli, HTTPClient &http, int timeout_ms)
{
    cli.setInsecure();
    cli.setHandshakeTimeout(25);
    cli.setTimeout((uint32_t)timeout_ms);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setConnectTimeout(timeout_ms);
    http.setTimeout(timeout_ms);
    http.setReuse(false);
    http.setUserAgent("MM1-BLACK");
}

bool http_get_text(const char *url, String &body, int timeout_ms)
{
    g_http_code = 0;
    WiFiClientSecure cli;
    HTTPClient http;
    secure_prep(cli, http, timeout_ms);
    if (!http.begin(cli, url)) {
        snprintf(g_status, sizeof(g_status), "Could not reach updates");
        return false;
    }
    g_http_code = http.GET();
    if (g_http_code != HTTP_CODE_OK) {
        http_status_from_code(g_http_code);
        Serial.printf("[OTA] GET %s -> %d\n", url, g_http_code);
        http.end();
        return false;
    }
    body = http.getString();
    http.end();
    return body.length() > 2;
}

void do_check(void)
{
    g_newer = false;
    g_latest[0] = '\0';
    g_rel_file[0] = '\0';
    snprintf(g_status, sizeof(g_status), "Looking for updates…");
    if (!sta_ready())
        return;

    String body;
    char tag[24] = "";
    char file[96] = "";
    bool got_manifest = false;
    for (int attempt = 0; attempt < 2 && !got_manifest; attempt++) {
        apply_public_dns();
        if (attempt)
            delay(400);
        body = "";
        tag[0] = '\0';
        file[0] = '\0';
        got_manifest =
            http_get_text(FW_GH_MANIFEST_URL, body, 20000) &&
            json_str_after(body.c_str(), FW_GH_BOARD_KEY, "\"tag\"", tag, sizeof(tag)) &&
            json_str_after(body.c_str(), FW_GH_BOARD_KEY, "\"file\"", file, sizeof(file));
    }
    if (!got_manifest) {
        const int first = g_http_code;
        body = "";
        if (!http_get_text(
                "https://api.github.com/repos/verlab/mm1-black/releases/latest",
                body, 20000)) {
            if (first == HTTP_CODE_NOT_FOUND)
                snprintf(g_status, sizeof(g_status), "No update listed yet");
            else if (!g_status[0] || strstr(g_status, "Looking"))
                snprintf(g_status, sizeof(g_status), "Could not reach updates");
            return;
        }
        if (!json_str_after(body.c_str(), nullptr, "\"tag_name\"", tag, sizeof(tag))) {
            snprintf(g_status, sizeof(g_status), "No update listed yet");
            return;
        }
        const char *pref = "MM1-BLACK-" FW_GH_BOARD_KEY "-";
        const char *p = body.c_str();
        bool found = false;
        while ((p = strstr(p, "\"name\"")) != nullptr) {
            char name[96] = "";
            if (json_str_after(p, nullptr, "\"name\"", name, sizeof(name)) &&
                strncmp(name, pref, strlen(pref)) == 0 &&
                strstr(name, ".bin") && !strstr(name, "sha256") &&
                json_str_after(p, nullptr, "\"browser_download_url\"", file,
                               sizeof(file))) {
                found = true;
                break;
            }
            p += 6;
        }
        if (!found) {
            snprintf(g_status, sizeof(g_status), "No update listed yet");
            return;
        }
    }
    strncpy(g_latest, tag, sizeof(g_latest) - 1);
    strncpy(g_rel_file, file, sizeof(g_rel_file) - 1);
    const int c = fw_gh_ver_cmp(tag, FW_VERSION);
    g_newer = (c > 0);
    if (c > 0)
        snprintf(g_status, sizeof(g_status), "New firmware %s", tag);
    else if (c == 0)
        snprintf(g_status, sizeof(g_status), "This tape is up to date");
    else
        snprintf(g_status, sizeof(g_status), "This tape is up to date");
}

void do_install(void)
{
    g_pct = 0;
    if (!sta_ready())
        return;
    if (!g_rel_file[0])
        do_check();
    if (!g_rel_file[0]) {
        if (!g_status[0])
            snprintf(g_status, sizeof(g_status), "Could not find an update");
        return;
    }
    if (!g_newer && fw_gh_ver_cmp(g_latest, FW_VERSION) <= 0) {
        snprintf(g_status, sizeof(g_status), "This tape is up to date");
        return;
    }

    char url[192];
    if (strncmp(g_rel_file, "http", 4) == 0)
        snprintf(url, sizeof(url), "%s", g_rel_file);
    else
        snprintf(url, sizeof(url), "%s%s", FW_GH_BASE_URL, g_rel_file);

    snprintf(g_status, sizeof(g_status), "Installing…");
    WiFiClientSecure cli;
    HTTPClient http;
    secure_prep(cli, http, 20000);
    http.setTimeout(60000);
    if (!http.begin(cli, url)) {
        snprintf(g_status, sizeof(g_status), "Could not install");
        return;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[OTA] install GET %d\n", code);
        snprintf(g_status, sizeof(g_status), "Could not install");
        http.end();
        return;
    }
    const int total = http.getSize();
    if (total > (int)0x5C0000L) {
        snprintf(g_status, sizeof(g_status), "Could not install");
        http.end();
        return;
    }
    if (!Update.begin(total > 0 ? (size_t)total : 0x5C0000UL)) {
        snprintf(g_status, sizeof(g_status), "Could not install");
        http.end();
        return;
    }

    WiFiClient *s = http.getStreamPtr();
    uint8_t buf[1024];
    size_t written = 0;
    int last_pct = -1;
    unsigned long last_ms = millis();
    while (http.connected() && (total < 0 || (int)written < total)) {
        const size_t avail = s->available();
        if (!avail) {
            if (millis() - last_ms > 20000UL)
                break;
            delay(5);
            yield();
            continue;
        }
        last_ms = millis();
        const int n = s->readBytes(buf, (avail > sizeof(buf)) ? sizeof(buf) : avail);
        if (n <= 0)
            break;
        if (Update.write(buf, (size_t)n) != (size_t)n) {
            snprintf(g_status, sizeof(g_status), "Could not install");
            Update.abort();
            http.end();
            return;
        }
        written += (size_t)n;
        if (total > 0) {
            g_pct = (int)((written * 100UL) / (size_t)total);
            if (g_pct != last_pct) {
                last_pct = g_pct;
                snprintf(g_status, sizeof(g_status), "Installing… %d%%", g_pct);
            }
        }
        yield();
    }
    http.end();
    if (!Update.end(true)) {
        snprintf(g_status, sizeof(g_status), "Could not install");
        return;
    }
    g_pct = 100;
    snprintf(g_status, sizeof(g_status), "Installed — restarting");
    delay(400);
    ESP.restart();
}

}  // namespace

int fw_gh_ver_cmp(const char *remote_tag, const char *local)
{
    int rm = 0, rn = 0, rp = 0, lm = 0, ln = 0, lp = 0;
    const bool rok = parse_trip(remote_tag, &rm, &rn, &rp) != 0;
    const bool lok = parse_trip(local, &lm, &ln, &lp) != 0;
    if (!rok)
        return 0;
    if (!lok)
        return 1;
    if (rm != lm)
        return (rm > lm) ? 1 : -1;
    if (rn != ln)
        return (rn > ln) ? 1 : -1;
    if (rp != lp)
        return (rp > lp) ? 1 : -1;
    return 0;
}

void fw_gh_ota_request_check(void)
{
    if (!g_busy)
        g_job = JobCheck;
}

void fw_gh_ota_request_install(void)
{
    if (!g_busy)
        g_job = JobInstall;
}

static void ota_task(void *arg)
{
    const Job j = (Job)(uintptr_t)arg;
    if (j == JobCheck)
        do_check();
    else if (j == JobInstall)
        do_install();
    g_busy = false;
    vTaskDelete(nullptr);
}

void fw_gh_ota_poll(void)
{
    if (g_busy || g_job == JobNone)
        return;
    const Job j = g_job;
    g_job = JobNone;
    g_busy = true;
    const BaseType_t ok = xTaskCreatePinnedToCore(
        ota_task, "mm1_ota", 24576, (void *)(uintptr_t)j, 1, nullptr, 0);
    if (ok != pdPASS) {
        snprintf(g_status, sizeof(g_status), "OTA task fail");
        g_busy = false;
    }
}

bool fw_gh_ota_busy(void) { return g_busy; }
bool fw_gh_ota_newer(void) { return g_newer; }
int  fw_gh_ota_percent(void) { return g_pct; }
const char *fw_gh_ota_status(void) { return g_status; }
const char *fw_gh_ota_latest_tag(void) { return g_latest; }

#endif
