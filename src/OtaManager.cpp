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

String OtaManager::getPendingUrl() const { return pendingUrl; }

bool OtaManager::isDownloading() const { return downloading; }

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
    String remoteVersion;
    int ret = checkForUpdate(remoteVersion, errMsg);
    if (ret != OTA_CHECK_OK)
        return ret;

    String dlUrl = pendingUrl; // 清单里的固定固件地址

    // 2. 有新版，下载并升级（网络可能中途断流，最多重试 3 次）
    for (int attempt = 1; attempt <= 3; attempt++)
    {
        DBG("[OTA] 第 %d 次尝试，从 %s 下载...\n", attempt, dlUrl.c_str());

        // 根据协议选择 HTTP / HTTPS 客户端
        bool isHttps = dlUrl.startsWith("https://");
        WiFiClient plainClient;
        WiFiClientSecure secureClient;
        HTTPClient http;
        http.setTimeout(30000); // 读超时 30 秒，避免慢网速下提前中断
        bool beginOk;
        if (isHttps)
        {
            secureClient.setInsecure();   // 跳过证书校验（家用可接受）
            beginOk = http.begin(secureClient, dlUrl);
        }
        else
        {
            beginOk = http.begin(plainClient, dlUrl);
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
        return OTA_CHECK_OK;
    }
    return OTA_CHECK_FAILED;
}

int OtaManager::checkForUpdate(String &remoteVersion, String &errMsg)
{
    if (url.length() == 0)
    {
        errMsg = "OTA url is empty";
        return OTA_CHECK_FAILED;
    }

    String remoteUrl;
    if (!fetchMetadata(remoteVersion, remoteUrl, errMsg))
        return OTA_CHECK_FAILED;

    DBG("[OTA] 本地版本 %s，远端版本 %s\n", FW_VERSION, remoteVersion.c_str());
    if (!isVersionNewer(remoteVersion, FW_VERSION))
    {
        errMsg = "已是最新版本 " + String(FW_VERSION);
        return OTA_CHECK_NO_UPDATE;
    }

    // 记住待下载地址：清单里若带固件地址则优先用清单的（与版本一一对应）
    pendingUrl = (remoteUrl.length() > 0) ? remoteUrl : url;
    return OTA_CHECK_OK;
}

bool OtaManager::beginDownload(const String &downloadUrl, String &errMsg)
{
    finishDownload();

    bool isHttps = downloadUrl.startsWith("https://");
    if (isHttps)
    {
        dlSecure = new WiFiClientSecure();
        dlSecure->setInsecure();   // 跳过证书校验（家用可接受）
        dlHttp = new HTTPClient();
        dlHttp->setTimeout(30000);
        if (!dlHttp->begin(*dlSecure, downloadUrl))
        {
            errMsg = "HTTP begin failed";
            finishDownload();
            return false;
        }
    }
    else
    {
        dlPlain = new WiFiClient();
        dlHttp = new HTTPClient();
        dlHttp->setTimeout(30000);
        if (!dlHttp->begin(*dlPlain, downloadUrl))
        {
            errMsg = "HTTP begin failed";
            finishDownload();
            return false;
        }
    }

    int code = dlHttp->GET();
    if (code != HTTP_CODE_OK)
    {
        errMsg = "HTTP GET " + String(code);
        finishDownload();
        return false;
    }

    contentLength = dlHttp->getSize(); // 可能为 -1（chunked）
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
        errMsg = "Update.begin err=" + String(Update.getError());
        finishDownload();
        return false;
    }

    downloading = true;
    totalRead = 0;
    lastDataMs = millis();
    DBG("[OTA] 开始下载: %s\n", downloadUrl.c_str());
    return true;
}

int OtaManager::processDownload(String &errMsg, int &progressPercent)
{
    progressPercent = -1;
    if (!downloading || dlHttp == nullptr)
        return OTA_DL_IDLE;

    Stream &s = dlHttp->getStream();
    uint8_t buf[2048];
    int got = 0;
    unsigned long t0 = millis();
    // 每轮尽量读满缓冲区，但最多 40ms，避免长期占用主循环
    while (got < (int)sizeof(buf) && millis() - t0 < 40)
    {
        int avail = s.available();
        if (avail <= 0)
            break;
        int toRead = (avail > (int)sizeof(buf) - got) ? (int)sizeof(buf) - got : avail;
        int r = s.readBytes(buf + got, toRead);
        if (r <= 0)
            break;
        got += r;
    }

    if (got > 0)
    {
        if (Update.write(buf, got) != (size_t)got)
        {
            errMsg = "Update.write err=" + String(Update.getError());
            finishDownload();
            return OTA_DL_ERROR;
        }
        totalRead += got;
        lastDataMs = millis();
    }

    // 判断下载是否结束：已知长度读满，或连接关闭且无剩余数据
    bool streamEnded = false;
    if (contentLength > 0 && totalRead >= (unsigned long)contentLength)
        streamEnded = true;
    else if (s.available() == 0 && !dlHttp->connected())
        streamEnded = true;

    if (streamEnded)
    {
        if (!Update.end(true))
        {
            errMsg = "Update.end err=" + String(Update.getError());
            finishDownload();
            return OTA_DL_ERROR;
        }
        DBG("[OTA] 下载完成，共 %lu 字节\n", totalRead);
        finishDownload();
        return OTA_DL_DONE;
    }

    if (millis() - lastDataMs > 20000)
    {
        errMsg = "下载超时";
        finishDownload();
        return OTA_DL_ERROR;
    }

    if (contentLength > 0)
        progressPercent = (int)(totalRead * 100 / contentLength);
    return OTA_DL_RUNNING;
}

void OtaManager::finishDownload()
{
    downloading = false;
    if (dlHttp != nullptr)
    {
        dlHttp->end();
        delete dlHttp;
        dlHttp = nullptr;
    }
    if (dlPlain != nullptr)
    {
        delete dlPlain;
        dlPlain = nullptr;
    }
    if (dlSecure != nullptr)
    {
        delete dlSecure;
        dlSecure = nullptr;
    }
    totalRead = 0;
    contentLength = -1;
}

bool OtaManager::fetchMetadata(String &remoteVersion, String &remoteUrl, String &errMsg)
{
    // 版本清单放在固件同目录：xxx/esp32_wifi.bin -> xxx/ota.json
    int slash = url.lastIndexOf('/');
    if (slash < 0)
    {
        errMsg = "OTA url invalid";
        return false;
    }
    String metaUrl = url.substring(0, slash + 1) + "ota.json";
    DBG("[OTA] 读取版本清单 %s\n", metaUrl.c_str());

    bool isHttps = metaUrl.startsWith("https://");
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    HTTPClient http;
    http.setTimeout(15000);
    bool beginOk;
    if (isHttps)
    {
        secureClient.setInsecure();
        beginOk = http.begin(secureClient, metaUrl);
    }
    else
    {
        beginOk = http.begin(plainClient, metaUrl);
    }
    if (!beginOk)
    {
        errMsg = "meta HTTP begin failed";
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        errMsg = "meta HTTP " + String(code);
        http.end();
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream()) != DeserializationError::Ok)
    {
        errMsg = "meta JSON 解析失败";
        http.end();
        return false;
    }
    remoteVersion = doc["version"] | "";
    remoteUrl = doc["url"] | "";
    http.end();
    if (remoteVersion.length() == 0)
    {
        errMsg = "meta 缺少 version 字段";
        return false;
    }
    return true;
}

bool OtaManager::isVersionNewer(const String &remote, const String &current)
{
    int r[3] = {0, 0, 0}, c[3] = {0, 0, 0};
    sscanf(remote.c_str(), "%d.%d.%d", &r[0], &r[1], &r[2]);
    sscanf(current.c_str(), "%d.%d.%d", &c[0], &c[1], &c[2]);
    for (int i = 0; i < 3; i++)
    {
        if (r[i] != c[i])
            return r[i] > c[i];
    }
    return false; // 完全相等 = 无需更新
}
