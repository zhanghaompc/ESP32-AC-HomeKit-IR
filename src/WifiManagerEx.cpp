#include "WifiManagerEx.h"
#include "LedManager.h"
#include "TimerManager.h"
#include "MqttManager.h"
#include "DeviceConfig.h"
#include "OtaManager.h"
#include <Arduino.h>
#include <FastLED.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <homespan.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <HTTPUpdate.h>

extern float envTemperature;
extern float enHumidity;
extern String lastProtocolName;
extern LedManager ledManager;
extern TimerManager timerManager;
extern OtaManager otaManager;
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
    ArduinoOTA.handle();
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

    // ArduinoOTA：PlatformIO 里 pio run -t upload --upload-port <设备IP> 即可无线烧录
    ArduinoOTA.setHostname(deviceApName().c_str());
    ArduinoOTA.onStart([]() { Serial.println("OTA 开始..."); });
    ArduinoOTA.onEnd([]() { Serial.println("\nOTA 结束，重启中..."); });
    ArduinoOTA.onError([](ota_error_t err) { Serial.printf("OTA 错误: %u\n", err); });
    ArduinoOTA.begin();

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
    // 云端 OTA：查询当前固件版本和升级地址
    server.on("/otaget", HTTP_GET, [this]() {
        server.send(200, "application/json",
                    "{\"fw\":\"" + otaManager.getVersion() +
                    "\",\"url\":\"" + otaManager.getUrl() + "\"}");
    });

    // 云端 OTA：设置升级地址（http://IP:8080/otaset?url=...）
    server.on("/otaset", HTTP_GET, [this]() {
        String u = server.arg("url");
        bool ok = otaManager.setUrl(u);
        server.send(200, "text/plain", ok ? "OTA URL saved" : "invalid url");
    });

    // 云端 OTA：立即检查更新（http://IP:8080/ota）
    server.on("/ota", HTTP_GET, [this]() {
        int ret = otaManager.checkUpdate();
        if (ret == HTTP_UPDATE_OK)
        {
            server.send(200, "text/plain", "OTA ok, rebooting...");
            delay(300);
            ESP.restart();
        }
        else
        {
            server.send(200, "text/plain", String("OTA fail: ") + httpUpdate.getLastErrorString());
        }
    });

    // 网页 OTA 升级页面
    server.on("/update", HTTP_GET, [this]() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<title>固件升级</title></head>"
                      "<body style='font-family:sans-serif;padding:20px;text-align:center'>"
                      "<h2>固件升级 (OTA)</h2>"
                      "<form method='POST' action='/update' enctype='multipart/form-data'>"
                      "<input type='file' name='firmware' accept='.bin'><br><br>"
                      "<button type='submit'>上传并升级</button></form>"
                      "<p style='color:#888;font-size:12px'>升级期间请勿断电，完成后设备自动重启</p>"
                      "</body></html>";
        server.send(200, "text/html", html);
    });

    // OTA 固件上传
    server.on("/update", HTTP_POST, [this]() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", Update.hasError() ? "升级失败" : "升级成功，正在重启...");
        delay(1000);
        ESP.restart();
    }, [this]() {
        HTTPUpload &upload = server.upload();
        if (upload.status == UPLOAD_FILE_START)
        {
            Serial.printf("OTA 上传开始: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                Update.printError(Serial);
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
                Update.printError(Serial);
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
            if (Update.end(true))
                Serial.printf("OTA 成功，重启中... (%d 字节)\n", upload.totalSize);
            else
                Update.printError(Serial);
        }
    });

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
