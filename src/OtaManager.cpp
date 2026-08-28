#include "OtaManager.h"
#include "DeviceConfig.h"
#include "Debug.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <HTTPUpdate.h>

// 把 Update 包装成 Stream，供 HTTPClient::writeToStream 使用（正确处理 chunked）
class UpdateStream : public Stream
{
public:
    size_t write(uint8_t b) override { return Update.write(&b, 1); }
    size_t write(const uint8_t *buffer, size_t size) override { return Update.write(const_cast<uint8_t *>(buffer), size); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

#define OTA_CONFIG_FILE "/ota.json"
// 用 jsDelivr CDN 镜像 GitHub 文件，国内网络可稳定访问
#define OTA_DEFAULT_URL "https://cdn.jsdelivr.net/gh/zhanghaompc/ESP32-AC-HomeKit-IR@master/firmware/esp32_wifi.bin"

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

int OtaManager::checkUpdate(String &errMsg)
{
    if (url.length() == 0)
    {
        errMsg = "OTA url is empty";
        return HTTP_UPDATE_FAILED;
    }

    // 网络可能中途断流，最多重试 3 次
    for (int attempt = 1; attempt <= 3; attempt++)
    {
        DBG("[OTA] 第 %d 次尝试，从 %s 下载...\n", attempt, url.c_str());

        // 根据协议选择 HTTP / HTTPS 客户端
        bool isHttps = url.startsWith("https://");
        WiFiClient plainClient;
        WiFiClientSecure secureClient;
        HTTPClient http;
        http.setTimeout(30000); // 读超时 30 秒，避免慢网速下提前中断
        bool beginOk;
        if (isHttps)
        {
            secureClient.setInsecure();   // 跳过证书校验（家用可接受）
            beginOk = http.begin(secureClient, url);
        }
        else
        {
            beginOk = http.begin(plainClient, url);
        }
        if (!beginOk)
        {
            errMsg = "HTTP begin failed";
            DBG("[OTA] HTTP begin 失败\n");
            continue;
        }
        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            errMsg = "HTTP GET " + String(code);
            http.end();
            continue;
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            errMsg = "Update.begin err=" + String(Update.getError());
            http.end();
            continue;
        }

        // writeToStream 能正确处理 chunked 传输（jsDelivr 不返回 Content-Length）
        UpdateStream us;
        int written = http.writeToStream(&us);
        // UPDATE_SIZE_UNKNOWN 时，必须传 true 告诉 end() 按实际写入字节收尾，
        // 否则会与未知总大小比对失败，触发 UPDATE_ERROR_ABORT (err=12)
        if (!Update.end(true))
        {
            errMsg = "Update.end err=" + String(Update.getError());
            http.end();
            continue;
        }
        http.end();
        DBG("[OTA] 升级完成，写入 %d 字节\n", written);
        return HTTP_UPDATE_OK;
    }
    return HTTP_UPDATE_FAILED;
}
