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

static String jsonEscape(String value)
{
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    value.replace("\n", "\\n");
    value.replace("\r", "\\r");
    return value;
}

WifiManagerEx::WifiManagerEx() : server(8080) {}

void WifiManagerEx::begin()
{
    // 允许 WiFi 驱动在短暂丢包/漫游后自动恢复，减少设备长期离线。
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    // 关闭省电睡眠，减少部分路由器下的丢包、断线和恢复失败。
    WiFi.setSleep(false);

    // WiFiManager 配网：非阻塞门户 + 自动重连
    wifiManager.setDebugOutput(false);
    wifiManager.setDarkMode(false);   // 白色主题
    wifiManager.setMinimumSignalQuality(0); // 不过滤弱信号，避免“搜不到 WiFi”
    wifiManager.setConfigPortalBlocking(false);
    wifiManager.setConfigPortalTimeout(180);
    wifiManager.setWiFiAutoReconnect(true);
    wifiManager.setSaveConfigCallback([this]() {
        String ssid = wifiManager.getWiFiSSID(false);
        String pass = wifiManager.getWiFiPass(false);
        if (ssid.length() > 0)
        {
            saveWifiCredentials(ssid, pass);
            homeSpan.setWifiCredentials(ssid.c_str(), pass.c_str());
            lastAttemptTime = 0;
            Serial.printf("WiFiManager 已保存凭据: %s\n", ssid.c_str());
        }
    });
}

void WifiManagerEx::enable()
{
    connectWiFi();
    startWebServer();
}

void WifiManagerEx::disable()
{
    stopConfigPortal();
    stopWebServer();
    disconnectWiFi();
}

void WifiManagerEx::loop()
{
    if (webServerActive)
    {
        server.handleClient();
    }
    ensureConfigPortal();
    if (configPortalActive)
        wifiManager.process();
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

    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    // 混合模式：无论有没有保存 WiFi，都先把热点打开，手机随时能连上配网页。
    if (!configPortalActive)
        startConfigPortal();

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
        if (!configPortalActive)
        {
            Serial.println(hasSaved ? "\nWiFi连接失败，开启混合配网门户并继续自动重试" : "\n未配置WiFi，启动混合配网门户");
            startConfigPortal();
        }
    }
    else
    {
        wifiConnected = true;
        syncHomeSpanWifi();
        stopConfigPortal();
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

        if (!configPortalActive)
        {
            startConfigPortal();
        }

        if (millis() - lastAttemptTime >= retryInterval)
        {
            Serial.println("尝试重连...");
            WiFi.mode(WIFI_AP_STA);
            WiFi.setAutoReconnect(true);
            WiFi.setSleep(false);
            WiFi.reconnect();
            if (WiFi.status() != WL_CONNECTED)
            {
                String ssid, pass;
                if (loadWifiCredentials(ssid, pass) && ssid.length() > 0)
                    WiFi.begin(ssid.c_str(), pass.c_str());
            }
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
            stopConfigPortal();
            ledManager.stopBlink();
            ledManager.off(); // WiFi 已连接 = 熄灭
            Serial.printf("WiFi已连接！IP: %s:8080\n", WiFi.localIP().toString().c_str());
            timerManager.syncTime();
        }
    }
}

void WifiManagerEx::startConfigPortal()
{
    if (configPortalActive)
        return;

    unsigned long now = millis();
    if (now - lastPortalAttemptTime < portalRetryInterval)
        return;
    lastPortalAttemptTime = now;

    String apName = deviceApName();   // 例如 ESP32AC_A1B2
    ledManager.setColor(CRGB::Green);

    Serial.printf("启动 WiFiManager 配网门户: %s (AP IP: 192.168.4.1)\n", apName.c_str());

    wifiManager.startConfigPortal(apName.c_str());
    // WiFiManager 在 STA 未连接时会先关掉 STA 再开 AP，这里立刻恢复 STA，
    // 保持“热点常开 + 自动重连旧 WiFi”的混合模式。
    WiFi.mode(WIFI_AP_STA);
    WiFi.enableSTA(true);
    configPortalActive = true;
    Serial.printf("WiFiManager 门户已开启，SSID=%s，等待手机连接...\n", apName.c_str());
}

void WifiManagerEx::ensureConfigPortal()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        if (configPortalActive)
            stopConfigPortal();
        return;
    }

    if (!configPortalActive)
        startConfigPortal();
}

void WifiManagerEx::stopConfigPortal()
{
    if (!configPortalActive)
        return;
    wifiManager.stopConfigPortal();
    configPortalActive = false;
    Serial.println("WiFiManager 配网门户已关闭");
}

void WifiManagerEx::setupConfigPortalHandlers()
{
    if (configHandlersReady)
        return;

    configServer.on("/", HTTP_GET, [this]() {
        configServer.send(200, "text/html; charset=utf-8", buildConfigPageHtml(deviceApName()));
    });

    configServer.on("/status", HTTP_GET, [this]() {
        String json = "{\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
                      ",\"portal\":" + String(configPortalActive ? "true" : "false") +
                      ",\"apip\":\"" + jsonEscape(WiFi.softAPIP().toString()) +
                      "\",\"ip\":\"" + jsonEscape(WiFi.localIP().toString()) +
                      "\",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"}";
        configServer.send(200, "application/json; charset=utf-8", json);
    });

    configServer.on("/scan", HTTP_GET, [this]() {
        // Keep the provisioning AP alive while scanning. The ESP32 can only
        // discover 2.4 GHz networks, so the page also supports manual SSID entry.
        WiFi.mode(WIFI_AP_STA);
        WiFi.scanDelete();
        int n = WiFi.scanNetworks(false, true, false, 500);
        if (n == WIFI_SCAN_RUNNING)
        {
            unsigned long deadline = millis() + 12000;
            while (n == WIFI_SCAN_RUNNING && millis() < deadline)
            {
                delay(50);
                n = WiFi.scanComplete();
            }
        }

        if (n < 0)
        {
            // A second attempt handles a scan left in progress by the WiFi
            // driver after a previous browser request was interrupted.
            WiFi.scanDelete();
            delay(100);
            n = WiFi.scanNetworks(false, true, false, 500);
        }

        Serial.printf("配网页面扫描结果: %d (mode=%d, AP=%s)\n",
                      n, (int)WiFi.getMode(), WiFi.softAPIP().toString().c_str());

        if (n < 0)
        {
            String message = n == WIFI_SCAN_RUNNING ? "扫描仍在进行，请稍后重试" : "ESP32 扫描失败，请重试";
            configServer.send(200, "application/json; charset=utf-8",
                              "{\"ok\":false,\"count\":0,\"code\":" + String(n) +
                                  ",\"message\":\"" + message + "\",\"items\":[]}");
            return;
        }

        String json = "{\"ok\":true,\"count\":" + String(n) + ",\"code\":0,\"items\":[";
        int visibleCount = 0;
        for (int i = 0; i < n; i++)
        {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0)
                continue;
            if (visibleCount++)
                json += ",";
            ssid.replace("\\", "\\\\");
            ssid.replace("\"", "\\\"");
            json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
                    ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
        }
        json += "],\"message\":\"" + String(visibleCount ? "扫描完成" : "没有发现可见的 2.4GHz WiFi") + "\"}";
        WiFi.scanDelete();
        configServer.send(200, "application/json; charset=utf-8", json);
    });

    configServer.on("/save", HTTP_POST, [this]() {
        String ssid = configServer.arg("ssid");
        String pass = configServer.arg("pass");
        ssid.trim();
        if (ssid.length() == 0)
        {
            configServer.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"请先选择要连接的 WiFi\"}");
            return;
        }

        saveWifiCredentials(ssid, pass);
        // Keep HomeSpan's NVS credentials in sync with the portal credentials.
        homeSpan.setWifiCredentials(ssid.c_str(), pass.c_str());
        lastAttemptTime = 0;
        WiFi.mode(WIFI_AP_STA);
        WiFi.setAutoReconnect(true);
        WiFi.setSleep(false);
        WiFi.begin(ssid.c_str(), pass.c_str());

        String message = "{\"ok\":true,\"message\":\"已保存，正在连接 " + jsonEscape(ssid) + "\"}";
        configServer.send(200, "application/json; charset=utf-8", message);
    });

    configServer.onNotFound([this]() {
        configServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        configServer.send(302, "text/plain", "");
    });

    configHandlersReady = true;
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
        ":root{--bg:#eef3f8;--card:#ffffff;--text:#102033;--muted:#66778a;--line:#d8e1ea;--blue:#0a84ff;--green:#28a745;--red:#d64545;--shadow:0 10px 30px rgba(16,32,51,.08)}"
        "*{box-sizing:border-box}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'PingFang SC','Microsoft YaHei',sans-serif;background:linear-gradient(180deg,#f6f9fc 0%,#eef3f8 100%);color:var(--text)}"
        ".wrap{max-width:560px;margin:0 auto;padding:18px 16px 28px}.hero{padding:10px 2px 14px}.eyebrow{display:inline-block;font-size:12px;color:var(--blue);font-weight:700;letter-spacing:.04em;text-transform:uppercase}.title{margin:8px 0 6px;font-size:28px;line-height:1.15}.sub{margin:0;color:var(--muted);font-size:14px;line-height:1.7}"
        ".card{background:var(--card);border:1px solid rgba(16,32,51,.08);border-radius:14px;box-shadow:var(--shadow);padding:16px;margin-top:14px}"
        ".status{display:flex;justify-content:space-between;gap:12px;align-items:center;padding:12px 14px;border-radius:12px;background:#f7fbff;border:1px solid var(--line);font-size:14px;line-height:1.5}.status b{display:block;font-size:16px;color:var(--text)}"
        ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:12px}.pill{padding:10px 12px;border-radius:12px;background:#f8fafc;border:1px solid var(--line);font-size:13px;color:var(--muted)}.pill b{display:block;color:var(--text);font-size:14px;margin-top:2px;word-break:break-all}"
        "label{display:block;margin:14px 0 6px;font-size:13px;color:var(--muted);font-weight:600}"
        "select,input{width:100%;padding:13px 14px;border:1px solid var(--line);border-radius:12px;background:#fff;font-size:15px;color:var(--text);outline:none}select:focus,input:focus{border-color:rgba(10,132,255,.55);box-shadow:0 0 0 3px rgba(10,132,255,.12)}"
        ".row{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}.btn{appearance:none;border:0;border-radius:12px;padding:13px 15px;font-size:15px;font-weight:600;cursor:pointer}.btn.primary{background:var(--blue);color:#fff;flex:1}.btn.secondary{background:#edf3f8;color:var(--text);border:1px solid var(--line)}.btn:disabled{opacity:.55;cursor:not-allowed}"
        ".hint{margin-top:12px;font-size:13px;color:var(--muted);line-height:1.6}.msg{margin-top:12px;min-height:20px;font-size:13px;line-height:1.5}.msg.ok{color:var(--green)}.msg.err{color:var(--red)}.msg.info{color:var(--blue)}"
        ".list{display:grid;gap:8px;margin-top:12px;max-height:240px;overflow:auto}.item{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:10px 12px;border:1px solid var(--line);border-radius:12px;background:#fff;font-size:14px}.item small{color:var(--muted)}"
        ".badge{display:inline-flex;align-items:center;padding:4px 8px;border-radius:999px;background:#eaf4ff;color:var(--blue);font-size:12px;font-weight:700}.badge.ok{background:#e8f6ec;color:var(--green)}"
        "</style></head><body><div class='wrap'>"
        "<div class='hero'><span class='eyebrow'>WiFi Provisioning</span><h1 class='title'>设备配网</h1>"
        "<p class='sub'>设备会同时保留热点和 STA 重连能力。换了环境也能继续找回它，不用等它自己死扛。</p></div>"
        "<div class='card'><div class='status'><div><span class='badge' id='portalBadge'>门户开启中</span><b id='connState'>正在连接现有 WiFi</b><span id='connDesc'>热点已开启，支持继续重连。</span></div><div style='text-align:right'><small style='color:var(--muted)'>AP</small><b id='apip'>192.168.4.1</b></div></div>"
        "<div class='grid'><div class='pill'>设备编号<b>" + apName + "</b></div><div class='pill'>MQTT 主题<b>" + deviceMqttBase() + "</b></div></div></div>"
        "<div class='card'><form id='wifiForm'><label for='ssid'>WiFi 名称</label><input id='ssid' name='ssid' list='wifiOptions' required placeholder='扫描后选择，或手动输入 2.4GHz WiFi 名称' autocomplete='off'><datalist id='wifiOptions'></datalist><div class='row'><button type='button' class='btn secondary' id='scanBtn' onclick='doScan()'>扫描 WiFi</button><button type='submit' class='btn primary'>保存并连接</button></div><label for='pass'>WiFi 密码</label><input type='password' name='pass' id='pass' placeholder='开放网络可留空' autocomplete='off'></form><div class='hint'>ESP32 只能连接 2.4GHz WiFi。如果扫描不到，也可以手动输入 WiFi 名称。</div><div id='msg' class='msg'></div></div>"
        "<div class='card'><div style='display:flex;justify-content:space-between;align-items:center'><div><b style='font-size:16px'>附近网络</b><div class='sub' style='font-size:13px'>点扫描后选择要连接的热点</div></div><span class='badge' id='scanBadge'>未扫描</span></div><div id='scanList' class='list'></div></div>"
        "<script>"
        "var msg=document.getElementById('msg');var scanBtn=document.getElementById('scanBtn');var scanList=document.getElementById('scanList');var connState=document.getElementById('connState');var connDesc=document.getElementById('connDesc');var apip=document.getElementById('apip');var portalBadge=document.getElementById('portalBadge');var scanBadge=document.getElementById('scanBadge');"
        "function setMsg(text,kind){msg.className='msg '+(kind||'');msg.textContent=text||'';}"
        "function refreshStatus(){fetch('/status').then(function(r){return r.json()}).then(function(s){apip.textContent=s.apip||'192.168.4.1';if(s.connected){portalBadge.textContent='STA 已连接';portalBadge.className='badge ok';connState.textContent='WiFi 已连接';connDesc.textContent=s.ssid?s.ssid+' · '+s.ip:s.ip;}else{portalBadge.textContent='门户开启中';portalBadge.className='badge';connState.textContent='正在尝试重连';connDesc.textContent='热点开放中，等待新的 WiFi 配置';}}).catch(function(){});}"
        "function renderScan(data){var list=data.items||[];scanList.innerHTML='';var options=document.getElementById('wifiOptions');options.innerHTML='';if(!list.length){scanBadge.textContent='无结果';return;}scanBadge.textContent=list.length+' 个结果';list.forEach(function(w){var option=document.createElement('option');option.value=w.ssid;options.appendChild(option);var row=document.createElement('button');row.type='button';row.className='item';row.onclick=function(){document.getElementById('ssid').value=w.ssid;setMsg('已选择 '+w.ssid,'info');};var left=document.createElement('div');left.innerHTML='<div>'+w.ssid+'</div><small>'+(w.open?'开放网络':'加密网络')+'</small>';var right=document.createElement('small');right.textContent=w.rssi+' dBm';row.appendChild(left);row.appendChild(right);scanList.appendChild(row);});}"
        "function doScan(){scanBtn.disabled=true;scanBadge.textContent='扫描中';setMsg('正在扫描附近 2.4GHz WiFi…','info');fetch('/scan').then(function(r){return r.json()}).then(function(data){renderScan(data);var list=data.items||[];setMsg(list.length?'扫描完成':(data.message||'没有扫到可用 WiFi，可手动输入名称'),'info');}).catch(function(){setMsg('扫描失败，请重试；也可以手动输入 WiFi 名称','err');scanBadge.textContent='失败';}).finally(function(){scanBtn.disabled=false;});}"
        "document.getElementById('wifiForm').addEventListener('submit',function(e){e.preventDefault();var ssid=document.getElementById('ssid').value.trim();var pass=document.getElementById('pass').value; if(!ssid){setMsg('请先选择一个 WiFi','err');return;}setMsg('正在保存并连接 '+ssid+'…','info');var form=new FormData();form.append('ssid',ssid);form.append('pass',pass);fetch('/save',{method:'POST',body:form}).then(function(r){return r.json()}).then(function(j){setMsg(j.message||'已保存，正在连接','ok');refreshStatus();}).catch(function(){setMsg('保存失败，请重试','err');});});"
        "refreshStatus();setInterval(refreshStatus,3000);"
        "</script></div></body></html>");
}

void WifiManagerEx::startWebServer()
{
    if (webServerActive)
        return;
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
    if (webHandlersReady)
        return;

    // 云端 OTA：查询当前固件版本和升级地址
    server.on("/otaget", HTTP_GET, [this]() {
        server.send(200, "application/json",
                    "{\"fw\":\"" + otaManager.getVersion() +
                    "\",\"url\":\"" + otaManager.getUrl() + "\"}");
    });

    // 只检查版本（不下载），返回 JSON 供调试/网页使用
    server.on("/otacheck", HTTP_GET, [this]() {
        String ver = "", err = "";
        int ret = otaManager.checkForUpdate(ver, err);
        String json = "{\"fw\":\"" + otaManager.getVersion() + "\",\"latest\":\"" +
                      (ret == OTA_CHECK_OK ? ver : otaManager.getVersion()) +
                      "\",\"update\":" + String(ret == OTA_CHECK_OK ? "true" : "false") + "}";
        server.send(200, "application/json", json);
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

    webHandlersReady = true;
}

void WifiManagerEx::handleWiFiEvent()
{
}
