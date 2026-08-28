#include "WifiManagerEx.h"
#include "LedManager.h"
#include "TimerManager.h"
#include "MqttManager.h"
#include "DeviceConfig.h"
#include "OtaManager.h"
#include <Arduino.h>
#include <FastLED.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
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
    String savedSsid, savedPass;
    bool hasSaved = loadWifiCredentials(savedSsid, savedPass);

    WiFi.mode(WIFI_STA);
    if (hasSaved)
    {
        Serial.printf("使用已保存的WiFi: %s\n", savedSsid.c_str());
        WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    }
    else
    {
        // 兼容旧固件：WiFiManager 曾把凭据写入 NVS，WiFi.begin() 能自动读取
        WiFi.begin();
    }
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
        if (!hasSaved && WiFi.SSID().length() == 0)
        {
            Serial.println("\n未配置WiFi，启动中文配置门户");
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
    String apName = deviceApName();   // 例如 ESP32AC_A1B2
    ledManager.setColor(CRGB::Green);

    Serial.printf("启动中文配置门户: %s (AP IP: 192.168.4.1)\n", apName.c_str());

    // 开一个无密码热点，手机/电脑连上后访问任意网址都会被引导到配网页
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apName.c_str());

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());

    configServer.on("/", HTTP_GET, [this]() {
        configServer.send(200, "text/html; charset=utf-8", buildConfigPageHtml(deviceApName()));
    });

    // 扫描周围 WiFi，返回 JSON 列表（页面先显示，点“扫描 WiFi”再异步拉取，秒开不卡）
    configServer.on("/scan", HTTP_GET, [this]() {
        int n = WiFi.scanNetworks();
        String json = "[";
        for (int i = 0; i < n; i++)
        {
            if (i) json += ",";
            String ssid = WiFi.SSID(i);
            ssid.replace("\\", "\\\\");
            ssid.replace("\"", "\\\"");
            json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
                    ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
        }
        json += "]";
        WiFi.scanDelete();
        configServer.send(200, "application/json", json);
    });

    // 保存 WiFi 凭据到 SPIFFS，重启后自动连接
    configServer.on("/save", HTTP_POST, [this]() {
        String ssid = configServer.arg("ssid");
        String pass = configServer.arg("pass");
        ssid.trim();
        if (ssid.length() == 0)
        {
            configServer.send(200, "text/html; charset=utf-8",
                              "<meta charset='utf-8'><h3>请先选择要连接的 WiFi</h3><a href='/'>返回</a>");
            return;
        }
        saveWifiCredentials(ssid, pass);
        String html = "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<body style='font-family:sans-serif;text-align:center;padding:40px'>"
                      "<h3>配置已保存</h3><p>正在连接 <b>" + ssid + "</b> ...</p>"
                      "<p>请关闭本页面，稍后到设备 MQTT 面板确认上线。</p></body>";
        configServer.send(200, "text/html; charset=utf-8", html);
        delay(500);
        ESP.restart();
    });

    // 手机打开任意网址（含系统探测的 captive 地址）都跳回配网页
    configServer.onNotFound([this]() {
        configServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        configServer.send(302, "text/plain", "");
    });

    configServer.begin(); // 必须调用 begin() 才会真正监听 80 端口
    Serial.println("配网门户已开启，等待手机连接...");
    unsigned long portalStart = millis();
    while (millis() - portalStart < 180000) // 3 分钟无人操作则重启
    {
        dnsServer.processNextRequest();
        configServer.handleClient();
        delay(1);
    }
    Serial.println("配网门户超时，重启设备");
    ESP.restart();
}

bool WifiManagerEx::loadWifiCredentials(String &ssid, String &pass)
{
    if (!SPIFFS.exists("/wifi.json"))
        return false;
    File f = SPIFFS.open("/wifi.json", "r");
    if (!f)
        return false;
    JsonDocument doc;
    bool ok = false;
    if (deserializeJson(doc, f) == DeserializationError::Ok)
    {
        ssid = doc["ssid"] | "";
        pass = doc["pass"] | "";
        ok = ssid.length() > 0;
    }
    f.close();
    return ok;
}

void WifiManagerEx::saveWifiCredentials(const String &ssid, const String &pass)
{
    JsonDocument doc;
    doc["ssid"] = ssid;
    doc["pass"] = pass;
    File f = SPIFFS.open("/wifi.json", "w");
    if (f)
    {
        serializeJson(doc, f);
        f.close();
        Serial.printf("WiFi 凭据已保存: %s\n", ssid.c_str());
    }
    else
    {
        Serial.println("保存 WiFi 凭据失败");
    }
}

String WifiManagerEx::buildConfigPageHtml(const String &apName)
{
    return String(
        "<!DOCTYPE html><html lang='zh-CN'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>设备配网</title><style>"
        "body{font-family:-apple-system,'PingFang SC','Microsoft YaHei',sans-serif;max-width:420px;margin:0 auto;padding:20px;background:#f5f6fa}"
        "h2{font-size:20px;color:#222}label{display:block;margin:14px 0 6px;color:#555;font-size:14px}"
        "select,input{width:100%;box-sizing:border-box;padding:12px;border:1px solid #ddd;border-radius:8px;font-size:15px;background:#fff}"
        "button{width:100%;margin-top:18px;padding:13px;border:0;border-radius:8px;background:#007aff;color:#fff;font-size:16px}"
        "button.ghost{background:#e9ecf1;color:#333;margin-top:8px;font-size:14px}"
        ".dev{background:#fff;border-radius:10px;padding:12px;font-size:13px;color:#666;margin-bottom:8px;line-height:1.7}"
        "#msg{color:#d9534f;font-size:13px;margin-top:10px;min-height:18px}"
        "</style></head><body>"
        "<h2>设备配网</h2>"
        "<div class='dev'>设备编号：<b>" + apName + "</b><br>MQTT 主题：<b>" + deviceMqttBase() + "</b></div>"
        "<form method='POST' action='/save' onsubmit='return checkForm()'>"
        "<label>选择 WiFi</label>"
        "<select id='ssid' name='ssid' required><option value=''>-- 点下方按钮扫描 --</option></select>"
        "<button type='button' class='ghost' id='scanBtn' onclick='doScan()'>扫描 WiFi</button>"
        "<label>WiFi 密码</label>"
        "<input type='password' name='pass' id='pass' placeholder='没有密码可留空' autocomplete='off'>"
        "<button type='submit'>保存并连接</button>"
        "</form><div id='msg'></div><script>"
        "function doScan(){"
        "var m=document.getElementById('msg'),b=document.getElementById('scanBtn'),s=document.getElementById('ssid');"
        "m.textContent='正在扫描…请稍候';m.style.color='#007aff';b.disabled=true;"
        "fetch('/scan').then(function(r){return r.json()}).then(function(list){"
        "b.disabled=false;m.textContent='';if(!list.length){m.textContent='没扫描到 WiFi，请重试';return;}"
        "s.innerHTML='<option value=\"\">-- 请选择 --</option>';"
        "list.forEach(function(w){var o=document.createElement('option');o.value=w.ssid;"
        "o.textContent=w.ssid+(w.open?' (开放)':'')+' ('+w.rssi+'dBm)';s.appendChild(o);});"
        "}).catch(function(){b.disabled=false;m.textContent='扫描失败，请重试';m.style.color='#d9534f';});}"
        "function checkForm(){var s=document.getElementById('ssid');if(!s.value){"
        "document.getElementById('msg').textContent='请先扫描并选择 WiFi';return false;}return true;}"
        "</script></body></html>");
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
        String otaErr = "";
        int ret = otaManager.checkUpdate(otaErr);
        if (ret == OTA_CHECK_OK)
        {
            server.send(200, "text/plain", "OTA ok, rebooting...");
            delay(300);
            ESP.restart();
        }
        else if (ret == OTA_CHECK_NO_UPDATE)
        {
            server.send(200, "text/plain", "OTA no update: " + otaErr);
        }
        else
        {
            server.send(200, "text/plain", "OTA fail: " + otaErr);
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
