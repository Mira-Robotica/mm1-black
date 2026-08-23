#include "fw_gh_ota.h"

#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <string.h>
#include <stdlib.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "firmware_version.h"
#include "mm1_log.h"
#include <esp_heap_caps.h>

static void apply_public_dns(void)
{
    /* AP+STA: outbound HTTPS must use the station iface. Do not replace a
     * working DHCP DNS — some networks block 8.8.8.8. */
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta)
        (void)esp_netif_set_default_netif(sta);

    if (WiFi.dnsIP()[0] != 0)
        return;

    (void)WiFi.setDNS(IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));
    ip_addr_t a0, a1;
    IP_ADDR4(&a0, 8, 8, 8, 8);
    IP_ADDR4(&a1, 1, 1, 1, 1);
    dns_setserver(0, &a0);
    dns_setserver(1, &a1);
    if (sta) {
        esp_netif_dns_info_t info = {};
        info.ip.type = ESP_IPADDR_TYPE_V4;
        info.ip.u_addr.ip4.addr = ESP_IP4TOADDR(8, 8, 8, 8);
        (void)esp_netif_set_dns_info(sta, ESP_NETIF_DNS_MAIN, &info);
        info.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
        (void)esp_netif_set_dns_info(sta, ESP_NETIF_DNS_BACKUP, &info);
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
    /* Confirm immediately. Waiting ~8 s let a later crash roll back to empty ota_1
     * (black screen, no splash on the next power-on). */
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        Serial.println("[OTA] boot confirmed");
}

namespace {

enum Job : uint8_t { JobNone = 0, JobCheck, JobInstall };

Job  g_job = JobNone;
bool g_busy = false;
bool g_newer = false;
int  g_pct = 0;
char g_status[80] = "idle";
char g_latest[24] = "";
char g_rel_file[128] = "";
int  g_rel_size = -1;

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
        snprintf(g_status, sizeof(g_status), "Still connecting...");
        return false;
    }
    apply_public_dns();
    return true;
}

void http_prep(HTTPClient &http, int timeout_ms)
{
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setConnectTimeout(timeout_ms);
    http.setTimeout(timeout_ms);
    http.setReuse(false);
    http.setUserAgent("MM1-BLACK");
    http.setAcceptEncoding("identity");
}

/* GitHub Pages (Fastly). Avoid Hosted UDP DNS — it fails on the C6 path. */
static const uint8_t k_pages_ip[][4] = {
    {185, 199, 108, 153},
    {185, 199, 110, 153},
    {185, 199, 111, 153},
};

static bool read_http_body(Client &c, String &body, int timeout_ms)
{
    const uint32_t deadline = millis() + (uint32_t)timeout_ms;
    char line[192];
    int idx = 0;
    bool headers = true;
    g_http_code = 0;
    body = "";
    while (millis() < deadline) {
        const int ch = c.read();
        if (ch < 0) {
            if (!c.connected() && !headers)
                break;
            delay(4);
            continue;
        }
        if (!headers) {
            body += (char)ch;
            continue;
        }
        if (ch == '\n') {
            if (idx && line[idx - 1] == '\r')
                idx--;
            line[idx] = '\0';
            if (idx == 0)
                headers = false;
            else if (!g_http_code && !strncmp(line, "HTTP/", 5)) {
                const char *sp = strchr(line, ' ');
                g_http_code = atoi(sp ? sp + 1 : "0");
            }
            idx = 0;
            continue;
        }
        if (idx < (int)sizeof(line) - 1)
            line[idx++] = (char)ch;
    }
    Serial.printf("[OTA] GET -> %d n=%u\n", g_http_code, (unsigned)body.length());
    return g_http_code == HTTP_CODE_OK && body.length() > 2;
}

static bool http_plain_get(const IPAddress &ip, uint16_t port, const char *host,
                           const char *path, String &body, int timeout_ms)
{
    WiFiClient c;
    c.setTimeout((uint32_t)timeout_ms);
    Serial.printf("[OTA] http %s:%u %s\n", ip.toString().c_str(),
                  (unsigned)port, path);
    if (!c.connect(ip, port)) {
        Serial.println("[OTA] tcp fail");
        return false;
    }
    char req[256];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: MM1-BLACK\r\n"
             "Accept: */*\r\nConnection: close\r\n\r\n",
             path, host);
    c.print(req);
    const bool ok = read_http_body(c, body, timeout_ms);
    c.stop();
    return ok;
}

static bool https_ip_get(const IPAddress &ip, const char *host, const char *path,
                         String &body, int timeout_ms)
{
    WiFiClientSecure *cli = new WiFiClientSecure();
    if (!cli)
        return false;
    cli->setInsecure();
    cli->setHandshakeTimeout(15);
    cli->setTimeout((uint32_t)timeout_ms);
    Serial.printf("[OTA] https %s %s\n", ip.toString().c_str(), path);
    const bool opened = cli->connect(ip, 443, host, nullptr, nullptr, nullptr);
    bool ok = false;
    if (!opened) {
        Serial.println("[OTA] tls fail");
    } else {
        char req[256];
        snprintf(req, sizeof(req),
                 "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: MM1-BLACK\r\n"
                 "Accept: */*\r\nConnection: close\r\n\r\n",
                 path, host);
        cli->print(req);
        ok = read_http_body(*cli, body, timeout_ms);
    }
    delete cli;
    return ok;
}

static const char *k_pages_host = "verlab.github.io";

static bool pages_tls_open(WiFiClientSecure &c)
{
    c.stop();
    c.setInsecure();
    c.setHandshakeTimeout(12);
    c.setTimeout(12000);
    for (unsigned i = 0; i < sizeof(k_pages_ip) / sizeof(k_pages_ip[0]); i++) {
        const IPAddress ip(k_pages_ip[i][0], k_pages_ip[i][1],
                           k_pages_ip[i][2], k_pages_ip[i][3]);
        Serial.printf("[OTA] tls %s\n", ip.toString().c_str());
        if (c.connect(ip, 443, k_pages_host, nullptr, nullptr, nullptr)) {
            Serial.println("[OTA] tls ok");
            return true;
        }
        Serial.println("[OTA] tls fail");
        c.stop();
        delay(40);
    }
    return false;
}

static bool fetch_manifest(String &body)
{
    apply_public_dns();
    /* Heap, not the OTA task stack — WiFiClientSecure is large. */
    WiFiClientSecure *c = new WiFiClientSecure();
    if (!c)
        return false;
    if (!pages_tls_open(*c)) {
        delete c;
        return false;
    }
    char req[256];
    snprintf(req, sizeof(req),
             "GET /mm1-black/latest.json HTTP/1.0\r\nHost: %s\r\n"
             "User-Agent: MM1-BLACK\r\nAccept: */*\r\n"
             "Connection: close\r\n\r\n",
             k_pages_host);
    c->print(req);
    const bool ok = read_http_body(*c, body, 12000);
    c->stop();
    delete c;
    return ok;
}

void do_check(void)
{
    g_newer = false;
    g_latest[0] = '\0';
    g_rel_file[0] = '\0';
    snprintf(g_status, sizeof(g_status), "Looking for updates...");
    if (!sta_ready())
        return;

    String body;
    char tag[24] = "";
    char file[128] = "";
    const bool got =
        fetch_manifest(body) &&
        json_str_after(body.c_str(), FW_GH_BOARD_KEY, "\"tag\"", tag, sizeof(tag)) &&
        json_str_after(body.c_str(), FW_GH_BOARD_KEY, "\"file\"", file, sizeof(file));
    if (!got) {
        if (g_http_code == HTTP_CODE_NOT_FOUND)
            snprintf(g_status, sizeof(g_status), "No update listed yet");
        else if (!g_status[0] || strstr(g_status, "Looking"))
            snprintf(g_status, sizeof(g_status), "Could not reach updates");
        return;
    }
    strncpy(g_latest, tag, sizeof(g_latest) - 1);
    strncpy(g_rel_file, file, sizeof(g_rel_file) - 1);
    g_rel_size = -1;
    {
        const char *sp = strstr(body.c_str(), "\"size\"");
        if (sp) {
            sp = strchr(sp, ':');
            if (sp)
                g_rel_size = atoi(sp + 1);
        }
    }
    const int c = fw_gh_ver_cmp(tag, FW_VERSION);
    g_newer = (c > 0);
    if (c > 0)
        snprintf(g_status, sizeof(g_status), "New firmware %s", tag);
    else
        snprintf(g_status, sizeof(g_status), "This tape is up to date");
}

static bool parse_http_url(const char *url, IPAddress &ip, uint16_t &port,
                           char *host, size_t hostsz, char *path, size_t pathsz)
{
    if (!url || strncmp(url, "http://", 7) != 0)
        return false;
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    port = 80;
    size_t hn = 0;
    if (colon && (!slash || colon < slash)) {
        hn = (size_t)(colon - p);
        port = (uint16_t)atoi(colon + 1);
    } else {
        hn = slash ? (size_t)(slash - p) : strlen(p);
    }
    if (hn == 0 || hn >= hostsz)
        return false;
    memcpy(host, p, hn);
    host[hn] = '\0';
    snprintf(path, pathsz, "%s", slash ? slash : "/");
    return ip.fromString(host);
}

static void ota_note(size_t written, int total)
{
    if (total > 0) {
        g_pct = (int)((written * 100UL) / (size_t)total);
        if (g_pct < 1 && written > 0)
            g_pct = 1;
        if (g_pct > 100)
            g_pct = 100;
        snprintf(g_status, sizeof(g_status), "Installing... %d%%", g_pct);
        } else {
        snprintf(g_status, sizeof(g_status), "Installing...");
    }
}

static bool write_ota(const uint8_t *p, size_t n)
{
    if (!n)
        return true;
    if (Update.write(const_cast<uint8_t *>(p), n) != n) {
        Serial.println("[OTA] Update.write fail");
        Update.abort();
        return false;
    }
    return true;
}

static bool stream_update(Client &c, int total)
{
    if (total > (int)0x5C0000L)
        return false;
    if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN)) {
        Serial.printf("[OTA] Update.begin fail %d err=%u\n",
                      total, (unsigned)Update.getError());
        return false;
    }
    c.setTimeout(15000);
    uint8_t buf[1024];
    size_t written = 0;
    snprintf(g_status, sizeof(g_status), "Installing...");
    while (total < 0 || (int)written < total) {
        size_t want = sizeof(buf);
        if (total > 0) {
            const size_t left = (size_t)total - written;
            if (left < want)
                want = left;
        }
        const int n = c.read(buf, want);
        if (n < 0)
            break;
        if (n == 0) {
            if (!c.connected() && written > 0)
                break;
            delay(2);
            continue;
        }
        if (!write_ota(buf, (size_t)n))
            return false;
        written += (size_t)n;
        ota_note(written, total);
        yield();
    }
    Serial.printf("[OTA] wrote %lu / %d\n", (unsigned long)written, total);
    if (total > 0 && (int)written != total) {
        Update.abort();
        return false;
    }
    return Update.end(true);
}

static int find_hdr_end(const uint8_t *b, int n)
{
    for (int i = 0; i + 3 < n; i++) {
        if (b[i] == '\r' && b[i + 1] == '\n' &&
            b[i + 2] == '\r' && b[i + 3] == '\n')
            return i + 4;
    }
    for (int i = 0; i + 1 < n; i++) {
        if (b[i] == '\n' && b[i + 1] == '\n')
            return i + 2;
    }
    return -1;
}

#ifndef HTTP_CODE_PARTIAL_CONTENT
#define HTTP_CODE_PARTIAL_CONTENT 206
#endif

/* Range GET on an already-open keep-alive socket (HTTP or TLS). */
static bool http_get_range_on(Client &c, const char *host, const char *path,
                              int off, int end, uint8_t *out, int outsz, int *got)
{
    *got = 0;
    char req[320];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: %s\r\nRange: bytes=%d-%d\r\n"
             "User-Agent: MM1-BLACK\r\nConnection: keep-alive\r\n\r\n",
             path, host, off, end);
    if (c.print(req) <= 0)
        return false;

    uint8_t acc[640];
    int acc_n = 0, hdr_end = -1;
    const uint32_t t0 = millis();
    while (hdr_end < 0 && millis() - t0 < 8000UL && acc_n < (int)sizeof(acc)) {
        const int r = c.read(acc + acc_n, (int)sizeof(acc) - acc_n);
        if (r < 0)
            break;
        if (r == 0) {
            delay(2);
            continue;
        }
        acc_n += r;
        hdr_end = find_hdr_end(acc, acc_n);
    }
    if (hdr_end < 0)
        return false;
    char save = (char)acc[hdr_end - 1];
    acc[hdr_end - 1] = 0;
    int code = 0, clen = -1;
    if (!strncmp((char *)acc, "HTTP/", 5)) {
        const char *sp = strchr((char *)acc, ' ');
        code = atoi(sp ? sp + 1 : "0");
    }
    const char *cl = strstr((char *)acc, "Content-Length:");
    if (!cl)
        cl = strstr((char *)acc, "content-length:");
    if (cl)
        clen = atoi(cl + 15);
    acc[hdr_end - 1] = (uint8_t)save;
    if (code != HTTP_CODE_PARTIAL_CONTENT && code != HTTP_CODE_OK)
        return false;
    const int want = end - off + 1;
    if (clen > want + 16 || clen > outsz) {
        Serial.printf("[OTA] range ignored clen=%d\n", clen);
        return false;
    }

    int n = acc_n - hdr_end;
    if (n > outsz)
        n = outsz;
    if (n > 0)
        memcpy(out, acc + hdr_end, (size_t)n);
    while (n < outsz && (clen < 0 || n < clen) && millis() - t0 < 12000UL) {
        const int r = c.read(out + n, ((clen > 0) ? clen : outsz) - n);
        if (r < 0)
            break;
        if (r == 0) {
            if (!c.connected())
                break;
            delay(2);
            continue;
        }
        n += r;
    }
    *got = n;
    return (clen > 0) ? (n == clen) : (n == want);
}

static bool ota_flash_from_buf(esp_ota_handle_t *h, const uint8_t *p, size_t n)
{
    const esp_err_t e = esp_ota_write(*h, p, n);
    if (e != ESP_OK) {
        Serial.printf("[OTA] write err=%d\n", (int)e);
        esp_ota_abort(*h);
        *h = 0;
        return false;
    }
    return true;
}

static bool ota_flash_image(const uint8_t *img, size_t got_all)
{
    mm1_log_hold(true);
    const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
    esp_ota_handle_t oh = 0;
    if (!part ||
        esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &oh) != ESP_OK) {
        Serial.println("[OTA] seq begin fail");
        mm1_log_hold(false);
        return false;
    }
    size_t put = 0;
    while (put < got_all) {
        size_t n = 4096;
        if (n > got_all - put)
            n = got_all - put;
        if (!ota_flash_from_buf(&oh, img + put, n)) {
            mm1_log_hold(false);
            return false;
        }
        put += n;
        yield();
    }
    const esp_err_t end_e = esp_ota_end(oh);
    if (end_e != ESP_OK) {
        Serial.printf("[OTA] end err=%d\n", (int)end_e);
        mm1_log_hold(false);
        return false;
    }
    const esp_err_t boot_e = esp_ota_set_boot_partition(part);
    mm1_log_hold(false);
    Serial.printf("[OTA] wrote %lu boot=%d\n", (unsigned long)got_all, (int)boot_e);
    return boot_e == ESP_OK;
}

static void pages_bin_path(char *path, size_t pathsz)
{
    if (strncmp(g_rel_file, "http", 4) == 0) {
        const char *p = strstr(g_rel_file, "://");
        p = p ? strchr(p + 3, '/') : nullptr;
        snprintf(path, pathsz, "%s", p ? p : "/");
        return;
    }
    if (g_rel_file[0] == '/')
        snprintf(path, pathsz, "%s", g_rel_file);
    else
        snprintf(path, pathsz, "/mm1-black/%s", g_rel_file);
}

static bool https_pages_install(const char *path)
{
    const int total = (g_rel_size > 0 && g_rel_size < (int)0x5C0000L)
                          ? g_rel_size : -1;
    if (total <= 0)
        return false;

    const int CHUNK = 1024;
    uint8_t *img = (uint8_t *)heap_caps_malloc((size_t)total,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!img) {
        Serial.println("[OTA] no PSRAM");
        return false;
    }

    snprintf(g_status, sizeof(g_status), "Installing... 1%%");
    g_pct = 1;
    WiFiClientSecure *psock = new WiFiClientSecure();
    if (!psock) {
        heap_caps_free(img);
        return false;
    }
    WiFiClientSecure &sock = *psock;
    bool open = false;
    size_t got_all = 0;
    while ((int)got_all < total) {
        const int off = (int)got_all;
        int end = off + CHUNK - 1;
        if (end >= total)
            end = total - 1;
        const int want = end - off + 1;
        int got = 0;
        bool ok = false;
        for (int t = 0; t < 4 && !ok; t++) {
            if (!open || !sock.connected()) {
                sock.stop();
                open = pages_tls_open(sock);
            }
            if (open)
                ok = http_get_range_on(sock, k_pages_host, path, off, end,
                                       img + off, want, &got)
                     && got == want;
            if (!ok) {
                sock.stop();
                open = false;
                delay(80);
            }
        }
        if (!ok) {
            Serial.printf("[OTA] range %d fail\n", off);
            sock.stop();
            delete psock;
            heap_caps_free(img);
            return false;
        }
        got_all += (size_t)got;
        ota_note(got_all, total);
        yield();
    }
    sock.stop();
    delete psock;
    const bool flashed = ota_flash_image(img, got_all);
    heap_caps_free(img);
    return flashed;
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

    char path[160] = "";
    pages_bin_path(path, sizeof(path));
    snprintf(g_status, sizeof(g_status), "Installing...");
    apply_public_dns();
    Serial.printf("[OTA] install %s\n", path);
    if (!https_pages_install(path)) {
        snprintf(g_status, sizeof(g_status), "Could not install");
        return;
    }
    g_pct = 100;
    snprintf(g_status, sizeof(g_status), "Installed - restarting");
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

static volatile bool g_task_live = false;
static bool          g_gave_up   = false;
static uint32_t      g_busy_since;
static Job           g_live_job  = JobNone;

void fw_gh_ota_request_check(void)
{
    if (!g_busy && !g_task_live) {
        g_job = JobCheck;
        snprintf(g_status, sizeof(g_status), "Looking for updates...");
    }
}

void fw_gh_ota_request_install(void)
{
    if (!g_busy && !g_task_live)
        g_job = JobInstall;
}

static void ota_task(void *arg)
{
    const Job j = (Job)(uintptr_t)arg;
    if (j == JobCheck)
        do_check();
    else if (j == JobInstall)
        do_install();
    g_task_live = false;
    g_gave_up = false;
    g_busy = false;
    g_live_job = JobNone;
    vTaskDelete(nullptr);
}

void fw_gh_ota_poll(void)
{
    if (g_task_live) {
        const uint32_t lim = (g_live_job == JobInstall) ? 360000UL : 60000UL;
        if (!g_gave_up && (millis() - g_busy_since) > lim) {
            g_gave_up = true;
            g_busy = false;
            if (g_live_job != JobInstall)
                snprintf(g_status, sizeof(g_status), "Could not reach updates");
            else
                snprintf(g_status, sizeof(g_status), "Could not install");
            Serial.println("[OTA] timed out");
        }
        return;
    }

    if (g_busy || g_job == JobNone)
        return;
    /* UI must already be painting. Never spawn on boot. Never PSRAM stacks. */
    if (millis() < 10000UL)
        return;

    const Job j = g_job;
    g_job = JobNone;
    g_busy = true;
    g_gave_up = false;
    g_busy_since = millis();
    g_live_job = j;
    g_task_live = true;
    BaseType_t ok = xTaskCreate(
        ota_task, "mm1_ota", 32768, (void *)(uintptr_t)j, 1, nullptr);
    if (ok != pdPASS)
        ok = xTaskCreate(
            ota_task, "mm1_ota", 20480, (void *)(uintptr_t)j, 1, nullptr);
    if (ok != pdPASS) {
        g_task_live = false;
        g_busy = false;
        g_live_job = JobNone;
        snprintf(g_status, sizeof(g_status), "Could not reach updates");
        Serial.printf("[OTA] no task heap=%lu\n", (unsigned long)ESP.getFreeHeap());
    }
}

bool fw_gh_ota_busy(void) { return g_busy; }
bool fw_gh_ota_newer(void) { return g_newer; }
int  fw_gh_ota_percent(void) { return g_pct; }
const char *fw_gh_ota_status(void) { return g_status; }
const char *fw_gh_ota_latest_tag(void) { return g_latest; }

#endif
