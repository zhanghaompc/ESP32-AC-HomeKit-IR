
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

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
    bool wifiConnected = false;
    bool webServerActive = false;
    void setupWebHandlers();
    void checkWiFiConnection();
    void handleWiFiEvent();
    void startWebServer();
    void stopWebServer();
    void connectWiFi();
    void disconnectWiFi();
    unsigned long lastAttemptTime = 0;
    const unsigned long retryInterval = 5000;
};
