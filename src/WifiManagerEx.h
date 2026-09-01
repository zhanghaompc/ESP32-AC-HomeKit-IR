
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// ============================================================
// WiFi 管理：本模块独占 WiFi 射频，HomeSpan 不参与连接管理。
//
// 连接策略（STA 与 AP 分工明确）：
//   1. 有凭据时先静默重连，退避间隔 5/10/20/30 秒封顶，此阶段不开 AP
//   2. 连续失败超过 staGraceMs 才拉起配网 AP（模式固定 WIFI_AP_STA，
//      STA 继续按退避节奏重试，不会因为开 AP 而失去重连能力）
//   3. STA 连上后关闭 AP —— AP_STA 长期共存会让 STA 吞吐和稳定性变差，
//      也避免用户在已联网状态下还看到一个多余的 SSID
//   4. 没有任何凭据时直接开 AP 等配网
// ============================================================

class WifiManagerEx
{
public:
    WifiManagerEx();
    void begin();
    void loop();
    void enable();
    void disable();
    bool isConnected() const;
    // 手动拉起配网热点（按键/指令触发），会重置退避计时
    void startConfigPortal();

private:
    WebServer server;       // 业务网页 :8080
    WebServer configServer; // 配网门户 :80
    DNSServer dnsServer;    // captive portal：全部域名指向 AP IP

    bool wifiConnected = false;
    bool webServerActive = false;
    bool configPortalActive = false;
    bool webHandlersReady = false;
    bool configHandlersReady = false;
    bool otaReady = false;
    bool hasCredentials = false;
    bool radioEnabled = false;

    // STA 重连退避：失败次数越多间隔越长，最长 30 秒
    uint8_t retryCount = 0;
    unsigned long lastStaAttemptTime = 0;
    unsigned long staDownSince = 0;
    // 断连多久后才开配网热点（这段时间留给静默重连）
    static const unsigned long staGraceMs = 30000;
    static const unsigned long apLingerMs = 1500;

    // 异步扫描状态：避免阻塞扫描打断 AP 广播导致手机掉线
    int scanState = -2; // -2=未开始 -1=进行中 >=0=结果数
    unsigned long scanStartTime = 0;
    static const unsigned long scanTimeoutMs = 15000;

    String pendingSsid;
    String pendingPass;

    void setupWebHandlers();
    void setupConfigPortalHandlers();
    void checkWiFiConnection();
    void beginStaConnect();
    // 已移除：handleWiFiEvent()（空实现）、ensureConfigPortal()（逻辑并入 checkWiFiConnection）
    unsigned long currentBackoff() const;
    void startWebServer();
    void stopWebServer();
    void startAccessPoint();
    void stopConfigPortal();
    void connectWiFi();
    void disconnectWiFi();
    void syncHomeSpanWifi();
    void handleScanRequest();
    bool loadWifiCredentials(String &ssid, String &pass);
    void saveWifiCredentials(const String &ssid, const String &pass);
    String buildConfigPageHtml(const String &apName);
};
