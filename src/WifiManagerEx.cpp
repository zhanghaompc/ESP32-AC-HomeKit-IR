#include "WifiManagerEx.h"
#include "LedManager.h"
#include "TimerManager.h"
#include "MqttManager.h"
#include "DeviceConfig.h"
#include <Arduino.h>
#include <FastLED.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <homespan.h>
#include <IRremoteESP8266.h>
#include <IRac.h>

extern float envTemperature;
extern float enHumidity;
extern String lastProtocolName;
extern LedManager ledManager;
extern TimerManager timerManager;
void AC_SET_DATA(int temp, int speed, int mode, bool power = true);
bool updateProtocolFromString(const String &, decode_type_t &);
extern IRac ac;

WifiManagerEx::WifiManagerEx() : server(8080) {}

void WifiManagerEx::begin()
{
}

void WifiManagerEx::enable()
{
    connectWiFi();
    startWebServer();
}

void WifiManagerEx::disable()
{
    disconnectWiFi();
    stopWebServer();
}

void WifiManagerEx::loop()
{
    if (webServerActive)
    {
        server.handleClient();
    }
    checkWiFiConnection();
}

bool WifiManagerEx::isConnected() const
{
    return wifiConnected;
}

void WifiManagerEx::connectWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    Serial.println("正在连接WiFi...");
    ledManager.blinkGreen();

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000)
    {
        delay(200);
        ledManager.update(); // 连接等待时保持绿灯闪烁动画
        Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        if (WiFi.SSID().length() == 0)
        {
            Serial.println("\n未配置WiFi，启动配置门户");
            startConfigPortal();
        }
        else
        {
            Serial.println("\nWiFi连接失败，保持绿灯闪烁并自动重试");
            // 不弹门户，由 checkWiFiConnection 持续闪烁重连
        }
    }
    else
    {
        wifiConnected = true;
        syncHomeSpanWifi();
        ledManager.stopBlink();
        ledManager.off(); // WiFi 已连接 = 熄灭
        Serial.printf("\nWiFi连接成功！IP: %s:8080\n", WiFi.localIP().toString().c_str());
        timerManager.syncTime();
    }
}

void WifiManagerEx::syncHomeSpanWifi()
{
    String ssid = WiFi.SSID();
    String pass = WiFi.psk();
    if (ssid.length() > 0)
    {
        homeSpan.setWifiCredentials(ssid.c_str(), pass.c_str());
        Serial.println("已同步HomeSpan WiFi凭据");
    }
}

void WifiManagerEx::disconnectWiFi()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiConnected = false;
    Serial.println("WiFi已关闭");
}

void WifiManagerEx::checkWiFiConnection()
{
    static bool wasConnected = false;

    if (WiFi.status() != WL_CONNECTED)
    {
        if (wasConnected)
        {
            wasConnected = false;
            wifiConnected = false;
            Serial.println("WiFi已断开，开始闪绿灯...");
            ledManager.blinkGreen();
        }
        else
        {
            ledManager.blinkGreen(); // 初始未连接也保持闪烁（同色去重，不会重置计时）
        }

        if (millis() - lastAttemptTime >= retryInterval)
        {
            Serial.println("尝试重连...");
            WiFi.reconnect();
            lastAttemptTime = millis();
        }
    }
    else
    {
        if (!wasConnected)
        {
            wasConnected = true;
            wifiConnected = true;
            syncHomeSpanWifi();
            ledManager.stopBlink();
            ledManager.off(); // WiFi 已连接 = 熄灭
            Serial.printf("WiFi已连接！IP: %s:8080\n", WiFi.localIP().toString().c_str());
            timerManager.syncTime();
        }
    }
}

void WifiManagerEx::startConfigPortal()
{
    WiFiManager wifiManager;
    String apName = deviceApName();   // 例如 ESP32AC_A1B2
    wifiManager.setTitle(("设备配网 " + apName).c_str());
    wifiManager.setTimeout(60);

    // 在配网页面上显示设备编号和推荐 MQTT 主题（后缀小写，直接复制即可）
    String deviceHtml = "<div style='padding:8px 0;font-size:14px;color:#666'>"
                        "设备编号：<b style='color:#222'>" + apName + "</b><br>"
                        "MQTT主题：<b style='color:#222'>" + deviceMqttBase() + "</b></div>";
    WiFiManagerParameter deviceParam(deviceHtml.c_str());
    wifiManager.addParameter(&deviceParam);

    // 门户是阻塞式的，主循环的LED刷新跑不到，改为常亮绿灯表示“配网等待中”，
    // 连接成功后由 checkWiFiConnection 熄灭
    ledManager.setColor(CRGB::Green);

    if (WiFi.status() != WL_CONNECTED)
    {
        if (!wifiManager.autoConnect(apName.c_str()))
        {
            Serial.println("WiFi连接失败，开启配置门户...");
            wifiManager.startConfigPortal(apName.c_str());
        }
    }
}

void WifiManagerEx::startWebServer()
{
    setupWebHandlers();
    server.begin();
    webServerActive = true;
    Serial.println("WebServer已启动");
}

void WifiManagerEx::stopWebServer()
{
    server.stop();
    webServerActive = false;
    Serial.println("WebServer已关闭");
}

void WifiManagerEx::setupWebHandlers()
{
    server.on("/", HTTP_GET, [this]()
              {
        File file = SPIFFS.open("/index.html", "r");
        if (!file) {
            server.send(404, "text/plain", "文件未找到");
            return;
        }
        String html = file.readString();
        file.close();
        server.send(200, "text/html", html); });

    server.on("/set", HTTP_GET, [this]()
              {
        String temp = server.arg("temp");
        String mode = server.arg("mode");
        String speed = server.arg("speed");
        String protocol = server.arg("protocol");
        int temperature = temp.toInt();
        int modeValue = mode.toInt();
        int speedValue = speed.toInt();
        if (!protocol.isEmpty()) {
            updateProtocolFromString(protocol, ac.next.protocol);
        }
        AC_SET_DATA(temperature, speedValue, modeValue);
        String response = "温度=" + temp + "°C, 模式=" + mode + ", 风速=" + speed;
        if (!protocol.isEmpty()) response += ", 协议=" + protocol;
        server.send(200, "text/plain", response); });

    server.on("/protocol", HTTP_GET, [this]()
              { server.send(200, "text/plain", lastProtocolName); });

    server.on("/sensor", HTTP_GET, [this]()
              {
        if (isnan(envTemperature) || isnan(enHumidity)) {
            server.send(500, "application/json", "{\"error\":\"传感器读取失败\"}");
            return;
        }
        String json = "{\"temp\":" + String(envTemperature, 1) + ",\"humidity\":" + String(enHumidity, 1) + "}";
        server.send(200, "application/json", json); });

    server.on("/mqttget", HTTP_GET, [this]()
              { server.send(200, "application/json", mqttManager.getConfigJson()); });

    server.on("/mqttset", HTTP_GET, [this]()
              {
        String host = server.arg("host");
        String port = server.arg("port");
        String user = server.arg("user");
        String pass = server.arg("pass");
        String topic = server.arg("topic");
        mqttManager.setConfig(host, port.isEmpty() ? 1883 : (uint16_t)port.toInt(), user, pass, topic);
        server.send(200, "text/plain", "MQTT config saved: " + mqttManager.getConfigJson()); });

    server.on("/power", HTTP_GET, [this]()
              {
        static bool powerState = false;
        powerState = !powerState;
        if (powerState) {
            AC_SET_DATA(26, 3, 1);
            server.send(200, "text/plain", "空调已开启");
        } else {
            ac.next.power = false;
            ac.sendAc();
            server.send(200, "text/plain", "空调已关闭");
        } });
}

void WifiManagerEx::handleWiFiEvent()
{
}
