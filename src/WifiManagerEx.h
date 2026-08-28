
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

class WifiManagerEx
{
public:
    WifiManagerEx();
    void begin();
    void loop();
    void enable();
    void disable();
    bool isConnected() const;
    void startConfigPortal();

private:
    WebServer server;
    WebServer configServer;
    DNSServer dnsServer;
    bool wifiConnected = false;
    bool webServerActive = false;
    void setupWebHandlers();
    void checkWiFiConnection();
    void handleWiFiEvent();
    void startWebServer();
    void stopWebServer();
    void connectWiFi();
    void disconnectWiFi();
    void syncHomeSpanWifi();
    bool loadWifiCredentials(String &ssid, String &pass);
    void saveWifiCredentials(const String &ssid, const String &pass);
    String buildConfigPageHtml(const String &apName);
    unsigned long lastAttemptTime = 0;
    const unsigned long retryInterval = 5000;
};
