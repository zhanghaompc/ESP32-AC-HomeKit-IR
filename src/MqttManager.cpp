#include "MqttManager.h"
#include "BleManager.h"
#include "Debug.h"
#include "DeviceConfig.h"
#include "OtaManager.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <IRac.h>
#include <IRremoteESP8266.h>

extern float envTemperature;
extern float enHumidity;
extern String lastProtocolName;
extern IRac ac;
extern OtaManager otaManager;

#define MQTT_CONFIG_FILE "/mqtt.json"

MqttManager::MqttManager() : client(wifiClient) {}

void MqttManager::begin()
{
    loadConfig();
    client.setBufferSize(1024);
    client.setKeepAlive(30);
    client.setSocketTimeout(15);
    client.setServer(host.c_str(), port);
    client.setCallback(MqttManager::onMessage);
    DBG("[MQTT] 初始化完成 host=%s port=%d topic=%s\n", host.c_str(), port, topic.c_str());
}

void MqttManager::loadConfig()
{
    if (!SPIFFS.exists(MQTT_CONFIG_FILE))
    {
        // 默认：EMQX 免费公共 broker（可在网页 http://<设备IP>:8080/mqttset 修改）
        host = "broker-cn.emqx.io";
        port = 1883;
        user = "";
        pass = "";
        topic = deviceMqttBase();   // 例如 ac/esp32aca1b2
        saveConfig();
        return;
    }

    File f = SPIFFS.open(MQTT_CONFIG_FILE, "r");
    if (!f)
    {
        host = "";
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err)
    {
        host = "";
        return;
    }
    host = doc["host"] | "";
    port = doc["port"] | 1883;
    user = doc["user"] | "";
    pass = doc["pass"] | "";
    topic = doc["topic"] | "";
}

void MqttManager::saveConfig()
{
    JsonDocument doc;
    doc["host"] = host;
    doc["port"] = port;
    doc["user"] = user;
    doc["pass"] = pass;
    doc["topic"] = topic;
    File f = SPIFFS.open(MQTT_CONFIG_FILE, "w");
    if (!f)
        return;
    serializeJson(doc, f);
    f.close();
    DBG("[MQTT] 配置已保存\n");
}

void MqttManager::setConfig(const String &h, uint16_t p, const String &u, const String &pw, const String &t)
{
    host = h;
    port = p;
    user = u;
    pass = pw;
    topic = t;
    saveConfig();
    if (client.connected())
        client.disconnect();
    DBG("[MQTT] 配置已更新\n");
}

String MqttManager::getConfigJson()
{
    JsonDocument doc;
    doc["host"] = host;
    doc["port"] = port;
    doc["user"] = user;
    doc["pass"] = pass;
    doc["topic"] = topic;
    String json;
    serializeJson(doc, json);
    return json;
}

void MqttManager::connect()
{
    if (host.length() == 0 || client.connected())
        return;

    String clientId = String("esp32ac-") + String((uint32_t)ESP.getEfuseMac(), HEX);
    DBG("[MQTT] 连接 %s:%d ...\n", host.c_str(), port);
    bool ok;
    if (user.length() > 0)
    {
        String willTopic = topic + "/status";
        String willMsg = "{\"online\":false}";
        ok = client.connect(clientId.c_str(), user.c_str(), pass.c_str(),
                            willTopic.c_str(), 1, true, willMsg.c_str());
    }
    else
    {
        String willTopic = topic + "/status";
        String willMsg = "{\"online\":false}";
        ok = client.connect(clientId.c_str(), willTopic.c_str(), 1, true, willMsg.c_str());
    }

    if (ok)
    {
        String inTopic = topic + "/in";
        client.subscribe(inTopic.c_str());
        DBG("[MQTT] 已连接，订阅 %s\n", inTopic.c_str());
        publishStatus(); // 上线立即上报一次状态
    }
    else
    {
        DBG("[MQTT] 连接失败 rc=%d\n", client.state());
    }
}

void MqttManager::loop()
{
    if (host.length() == 0)
        return;
    if (WiFi.status() != WL_CONNECTED)
    {
        // WiFi 断开时主动清理旧 MQTT socket，WiFi 恢复后才能重新握手。
        if (client.connected())
            client.disconnect();
        return;
    }
    if (!client.connected())
    {
        unsigned long now = millis();
        if (now - lastConnectAttempt >= 5000)
        {
            lastConnectAttempt = now;
            connect();
        }
    }
    client.loop();

    // 每 10 秒周期发布一次状态
    unsigned long now = millis();
    if (now - lastStatusTime >= 10000)
    {
        lastStatusTime = now;
        publishStatus();
    }
}

void MqttManager::publishStatus()
{
    if (!client.connected() || topic.length() == 0)
        return;

    JsonDocument doc;
    doc["temp"] = envTemperature;
    doc["humidity"] = enHumidity;
    doc["power"] = ac.next.power;
    doc["mode"] = (int)ac.next.mode;
    doc["speed"] = (int)ac.next.fanspeed;
    doc["degrees"] = ac.next.degrees;
    doc["protocol"] = lastProtocolName;
    doc["online"] = true;   // 设备在线标志（配合 LWT 离线消息）
    doc["turbo"] = ac.next.turbo;
    doc["swing"] = ac.next.swingv != stdAc::swingv_t::kOff;
    doc["light"] = ac.next.light;
    doc["sleep"] = ac.next.sleep >= 0;
    doc["clean"] = ac.next.clean;
    doc["fw"] = otaManager.getVersion();

    String json;
    serializeJson(doc, json);
    String statusTopic = topic + "/status";
    client.publish(statusTopic.c_str(), json.c_str(), true); // retained：新订阅者立刻拿到最新状态
    DBG("[MQTT] 发布状态: %s\n", json.c_str());
}

void MqttManager::publish(const String &payload)
{
    if (!client.connected() || topic.length() == 0)
        return;
    String outTopic = topic + "/out";
    client.publish(outTopic.c_str(), payload.c_str());
    DBG("[MQTT] 发布响应 %s: %s\n", outTopic.c_str(), payload.c_str());
}

void MqttManager::disconnect()
{
    if (client.connected())
        client.disconnect();
    DBG("[MQTT] 已断开\n");
}

bool MqttManager::isConnected()
{
    return client.connected();
}

void MqttManager::handleMessage(char *topic, byte *payload, unsigned int length)
{
    String msg;
    for (unsigned int i = 0; i < length; i++)
        msg += (char)payload[i];
    DBG("[MQTT] 收到指令: %s\n", msg.c_str());
    // 复用 BLE 同一套指令处理逻辑（开机/调温/模式/风速/协议/定时/查询等）
    bleManager.handleCommand(msg);
}

void MqttManager::onMessage(char *topic, byte *payload, unsigned int length)
{
    mqttManager.handleMessage(topic, payload, length);
}
