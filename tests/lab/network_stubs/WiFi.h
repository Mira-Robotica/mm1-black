#pragma once
#include <Arduino.h>
#include <string>
#include <memory>
using String=std::string;
inline std::deque<char> net_rx;
inline bool accept_pending=false;
struct WiFiClient {
    std::shared_ptr<bool> live;
    WiFiClient()=default;
    explicit WiFiClient(bool connected):live(std::make_shared<bool>(connected)){}
    bool connected() { return live && *live; }
    explicit operator bool() { return connected(); }
    void stop() { if(live) *live=false; }
    void setNoDelay(bool) {}
    int fd() { return 1; }
    int available() { return net_rx.size(); }
    int read() { auto c=net_rx.front();net_rx.pop_front();return c; }
};
struct WiFiServer {
    WiFiServer(int,int) {}
    void begin() {}
    void end() {}
    explicit operator bool() { return true; }
    WiFiClient accept() { if(accept_pending) {accept_pending=false;return WiFiClient(true);} return {}; }
};
struct IPAddress {
    String toString() const { return "127.0.0.1"; }
    explicit operator uint32_t() const { return 1; }
};
constexpr int WIFI_STA=1,WL_CONNECTED=3;
using arduino_event_id_t=int;
struct arduino_event_info_t { struct { unsigned reason; } wifi_sta_disconnected; };
constexpr int ARDUINO_EVENT_WIFI_STA_DISCONNECTED=1,ARDUINO_EVENT_WIFI_STA_LOST_IP=2;
struct FakeWiFi {
    int online=WL_CONNECTED;
    bool mode(int) { return true; }
    bool setSleep(bool) { return true; }
    void begin(const char*,const char*) {}
    void persistent(bool) {}
    void setAutoReconnect(bool) {}
    void setHostname(const char*) {}
    template<class F> void onEvent(F) {}
    int status() { return online; }
    IPAddress localIP() { return {}; }
    IPAddress subnetMask() { return {}; }
    IPAddress gatewayIP() { return {}; }
    int RSSI() { return -30; }
    void disconnect(bool,bool) {}
};
inline FakeWiFi WiFi;
