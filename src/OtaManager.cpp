#include "OtaManager.h"
#include "DeviceConfig.h"
#include "Debug.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>

#define OTA_CONFIG_FILE "/ota.json"
#define OTA_DEFAULT_URL "https://raw.githubusercontent.com/zhanghaompc/ESP32-AC-HomeKit-IR/main/firmware/esp32_wifi.bin"

void OtaManager::begin()
{
    if (!SPIFFS.exists(OTA_CONFIG_FILE))
    {
        url = OTA_DEFAULT_URL;
        saveConfig();
        return;
    }
    File f = SPIFFS.open(OTA_CONFIG_FILE, "r");
    if (!f)
    {
        url = OTA_DEFAULT_URL;
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok)
        url = doc["url"] | OTA_DEFAULT_URL;
    f.close();
}

void OtaManager::saveConfig()
{
    JsonDocument doc;
    doc["url"] = url;
    File f = SPIFFS.open(OTA_CONFIG_FILE, "w");
    if (f)
    {
        serializeJson(doc, f);
        f.close();
    }
}

String OtaManager::getUrl() const { return url; }

String OtaManager::getVersion() const { return FW_VERSION; }

bool OtaManager::setUrl(const String &u)
{
    if (u.length() < 10 || !u.startsWith("http"))
        return false;
    url = u;
    saveConfig();
    return true;
}

int OtaManager::checkUpdate()
{
    if (url.length() == 0)
        return HTTP_UPDATE_FAILED;
    DBG("[OTA] 从 %s 检查更新...\n", url.c_str());
    WiFiClientSecure client;
    client.setInsecure();   // GitHub https，跳过证书校验（家用可接受）
    t_httpUpdate_return ret = httpUpdate.update(client, url);
    return (int)ret;
}
