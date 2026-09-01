#pragma once
#include <Arduino.h>

// 固件版本号（OTA 状态显示用）
#define FW_VERSION "1.5.20"

// ============================================================
// 设备编号：基于 ESP32 eFuse MAC 的后 4 位十六进制
// 用于生成唯一的 WiFi 热点名和 MQTT 主题前缀
// 例：热点 ESP32AC_a1b2，主题 ac/esp32/a1b2（后缀统一小写，避免大小写混淆）
// ============================================================

inline String deviceSuffix()
{
    static String s;
    if (s.length() == 0)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%04x", (uint32_t)(ESP.getEfuseMac() & 0xFFFF));
        s = buf;
    }
    return s;
}

// WiFi 热点名：ESP32AC_xxxx（小写，与 MQTT 主题后缀保持一致）
inline String deviceApName()
{
    return "ESP32AC_" + deviceSuffix();
}

// MQTT 主题前缀：ac/esp32/xxxx（小写）
inline String deviceMqttBase()
{
    return "ac/esp32/" + deviceSuffix();
}

inline String mqttTopicSuffix(const String &topic)
{
    const char *prefixes[] = {"ac/esp32ac", "ac/esp8266ac", "ac/esp32/", "ac/esp8266/", "ac/esp32", "ac/esp8266"};
    for (const char *prefix : prefixes)
    {
        if (topic.startsWith(prefix))
        {
            String suffix = topic.substring(strlen(prefix));
            suffix.trim();
            return suffix;
        }
    }
    return "";
}

// 旧主题格式自动迁移到当前芯片格式，避免记忆里串成 esp8266 + esp32 后缀。
inline String normalizeMqttTopic(const String &topic)
{
    if (topic.startsWith("ac/esp32") || topic.startsWith("ac/esp8266"))
    {
        String suffix = mqttTopicSuffix(topic);
        return suffix.length() > 0 ? deviceMqttBase() + suffix : deviceMqttBase();
    }
    return topic;
}
