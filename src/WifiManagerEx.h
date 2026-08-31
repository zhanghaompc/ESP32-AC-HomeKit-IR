
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>

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
    WiFiManager wifiManager;
    bool wifiConnected = false;
    bool webServerActive = false;
    bool configPortalActive = false;
    bool webHandlersReady = false;
    bool configHandlersReady = false;
    void setupWebHandlers();
    void setupConfigPortalHandlers();
    void ensureConfigPortal();
    void checkWiFiConnection();
    void handleWiFiEvent();
    void startWebServer();
    void stopWebServer();
    void stopConfigPortal();
    void connectWiFi();
    void disconnectWiFi();
    void syncHomeSpanWifi();
    bool loadWifiCredentials(String &ssid, String &pass);
    void saveWifiCredentials(const String &ssid, const String &pass);
    String buildConfigPageHtml(const String &apName);
    unsigned long lastAttemptTime = 0;
    unsigned long lastPortalAttemptTime = 0;
    unsigned long lastApRestartTime = 0;
    const unsigned long retryInterval = 5000;
    const unsigned long portalRetryInterval = 2000;
    const unsigned long apRestartInterval = 30000;
};
