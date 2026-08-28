#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// MQTT 远程控制管理：复用 BLE 同一套文本指令（temp=..;mode=..;speed=.. / power=off / protocol=.. / timer=..）
// 主题约定：
//   <topic>/in      —— 下发指令（订阅）
//   <topic>/out     —— 指令响应（发布）
//   <topic>/status  —— 状态 JSON（发布，retained）
class MqttManager
{
public:
    MqttManager();
    void begin();               // 从 SPIFFS 读取配置并初始化客户端
    void loop();                // 保持连接、处理消息、周期发布状态（仅在 WiFi 模式调用）
    void publish(const String &payload);   // 发布响应到 <topic>/out
    void publishStatus();       // 发布温湿度/空调状态到 <topic>/status
    void disconnect();          // 主动断开（切回 BLE 模式时调用）
    bool isConnected();
    void setConfig(const String &host, uint16_t port, const String &user, const String &pass, const String &topic);
    String getConfigJson();

private:
    WiFiClient wifiClient;
    PubSubClient client;
    String host = "";
    uint16_t port = 1883;
    String user = "";
    String pass = "";
    String topic = "";
    unsigned long lastStatusTime = 0;
    unsigned long lastConnectAttempt = 0;

    void loadConfig();
    void saveConfig();
    void connect();
    void handleMessage(char *topic, byte *payload, unsigned int length);
    static void onMessage(char *topic, byte *payload, unsigned int length);
};

extern MqttManager mqttManager;
