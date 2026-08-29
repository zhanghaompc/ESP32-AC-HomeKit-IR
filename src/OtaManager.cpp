#include "OtaManager.h"
#include "DeviceConfig.h"
#include "Debug.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <HTTPUpdate.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#ifndef OTA_ENV_NAME
#define OTA_ENV_NAME "esp32_wifi"
#endif

static const char *OTA_REPO = "zhanghaompc/ESP32-AC-HomeKit-IR";
static const char *OTA_MANIFEST_BASES[] = {
    // 版本清单要尽量实时；raw.githubusercontent.com 几乎与 push 同步。
    // Fastly 之前长期返回旧 master 缓存，因此只作为最后兜底。
    "https://raw.githubusercontent.com/zhanghaompc/ESP32-AC-HomeKit-IR/master",
    "https://cdn.jsdelivr.net/gh/zhanghaompc/ESP32-AC-HomeKit-IR@master",
    "https://fastly.jsdelivr.net/gh/zhanghaompc/ESP32-AC-HomeKit-IR@master"};

#define OTA_TASK_STACK_SIZE 10240

class OtaManager;

// 把 Update 包装成 Stream，供 OTA 任务里的 writeToStream 使用。
// 与阻塞式 checkUpdate() 使用的 UpdateStream 不同，这个类会把写入进度回报给 OtaManager。
class OtaUpdateStream : public Stream
{
public:
    OtaUpdateStream(OtaManager *mgr, int len) : owner(mgr), contentLength(len) {}

    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *buffer, size_t size) override
    {
        size_t n = Update.write(const_cast<uint8_t *>(buffer), size);
        if (n > 0 && owner != nullptr)
            owner->onDownloadProgress(n, contentLength);
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

private:
    OtaManager *owner;
    int contentLength;
};

static String otaDefaultUrl()
{
    return String("https://fastly.jsdelivr.net/gh/") + OTA_REPO + "@master/firmware/" + OTA_ENV_NAME + ".bin";
}

static String otaManifestUrl(const char *base)
{
    return String(base) + "/firmware/ota_" + OTA_ENV_NAME + ".json";
}

static String otaLegacyManifestUrl(const char *base)
{
    return String(base) + "/firmware/ota.json";
}

static bool extractManifestString(JsonDocument &doc, const char *field, const char *env, String &out)
{
    JsonVariantConst node = doc[field];
    if (node.is<const char *>())
    {
        out = node.as<const char *>();
        return out.length() > 0;
    }

    if (node.is<JsonObjectConst>())
    {
        JsonObjectConst obj = node.as<JsonObjectConst>();
        if (obj[env].is<const char *>())
        {
            out = obj[env].as<const char *>();
            return out.length() > 0;
        }
        if (obj["default"].is<const char *>())
        {
            out = obj["default"].as<const char *>();
            return out.length() > 0;
        }
    }

    return false;
}

static bool fetchText(HTTPClient &http, String &text)
{
    Stream &s = http.getStream();
    text = "";
    while (http.connected() || s.available())
    {
        while (s.available())
        {
            int c = s.read();
            if (c < 0)
                break;
            text += (char)c;
        }
        delay(1);
    }
    return text.length() > 0;
}

#define OTA_CONFIG_FILE "/ota.json"

void OtaManager::begin()
{
    if (otaStateMutex == nullptr)
        otaStateMutex = xSemaphoreCreateMutex();

    if (!SPIFFS.exists(OTA_CONFIG_FILE))
    {
        url = otaDefaultUrl();
        saveConfig();
    }
    else
    {
        File f = SPIFFS.open(OTA_CONFIG_FILE, "r");
        if (!f)
        {
            url = otaDefaultUrl();
        }
        else
        {
            JsonDocument doc;
            if (deserializeJson(doc, f) == DeserializationError::Ok)
                url = doc["url"] | otaDefaultUrl();
            f.close();
        }
    }

    // 早期版本默认域名是 cdn.jsdelivr.net（国内不稳定），启动时自动迁移到 fastly
    if (url.indexOf("cdn.jsdelivr.net") >= 0)
    {
        DBG("[OTA] 迁移 OTA 地址到 fastly 节点\n");
        url.replace("cdn.jsdelivr.net", "fastly.jsdelivr.net");
        saveConfig();
    }
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

    // 有新版后，复用同一个异步 OTA 下载任务；这样网页 / BLE 的阻塞式入口
    // 也不会在当前调用栈里执行 TLS/HTTP 下载，避免阻塞各自协议栈。
    for (int attempt = 1; attempt <= 3; attempt++)
    {
        DBG("[OTA] 第 %d 次异步下载尝试\n", attempt);
        String downloadErr = "";
        if (!beginDownload(pendingUrl, downloadErr))
        {
            errMsg = downloadErr;
            delay(500);
            continue;
        }

        unsigned long start = millis();
        while (true)
        {
            String taskErr = "";
            int pct = -1;
            int st = processDownload(taskErr, pct);
            if (st == OTA_DL_DONE)
                return OTA_CHECK_OK;
            if (st == OTA_DL_ERROR)
            {
                errMsg = taskErr;
                break;
            }
            if (millis() - start > 180000UL)
            {
                errMsg = "OTA timeout";
                break;
            }
            delay(20);
        }
    }
    return OTA_CHECK_FAILED;
}

int OtaManager::checkForUpdate(String &remoteVersion, String &errMsg)
{
    String remoteUrl;
    if (!fetchMetadata(remoteVersion, remoteUrl, errMsg))
        return OTA_CHECK_FAILED;

    DBG("[OTA] 本地版本 %s，远端版本 %s\n", FW_VERSION, remoteVersion.c_str());
    if (!isVersionNewer(remoteVersion, FW_VERSION))
    {
        errMsg = "已是最新版本 " + String(FW_VERSION);
        return OTA_CHECK_NO_UPDATE;
    }

    // 记住待下载地址：优先用清单里的（与版本一一对应）
    pendingUrl = (remoteUrl.length() > 0) ? remoteUrl : (url.length() > 0 ? url : otaDefaultUrl());
    return OTA_CHECK_OK;
}

bool OtaManager::beginDownload(const String &downloadUrl, String &errMsg)
{
    if (downloadUrl.length() < 10 || !downloadUrl.startsWith("http"))
    {
        errMsg = "invalid download url";
        return false;
    }

    if (otaStateMutex == nullptr)
        otaStateMutex = xSemaphoreCreateMutex();

    if (otaStateMutex != nullptr)
        xSemaphoreTake(otaStateMutex, portMAX_DELAY);
    if (otaTaskRunning)
    {
        if (otaStateMutex != nullptr)
            xSemaphoreGive(otaStateMutex);
        errMsg = "OTA already running";
        return false;
    }
    otaTaskRunning = true;
    if (otaStateMutex != nullptr)
        xSemaphoreGive(otaStateMutex);

    activeDownloadUrl = downloadUrl;
    downloadBytes = 0;
    setDownloadState(OTA_DL_RUNNING, -1, "");

    BaseType_t created = xTaskCreate(otaTaskEntry, "otaTask", OTA_TASK_STACK_SIZE, this, 1, &otaTaskHandle);
    if (created != pdPASS)
    {
        if (otaStateMutex != nullptr)
        {
            xSemaphoreTake(otaStateMutex, portMAX_DELAY);
            otaTaskRunning = false;
            xSemaphoreGive(otaStateMutex);
        }
        errMsg = "create ota task failed";
        setDownloadState(OTA_DL_IDLE, -1, errMsg);
        return false;
    }

    DBG("[OTA] 开始异步下载: %s\n", downloadUrl.c_str());
    return true;
}

int OtaManager::processDownload(String &errMsg, int &progressPercent)
{
    OtaDownloadStatus st = OTA_DL_IDLE;
    int pct = -1;
    String err = "";
    getDownloadState(st, pct, err);
    errMsg = err;
    progressPercent = pct;
    return (int)st;
}

bool OtaManager::isDownloading()
{
    OtaDownloadStatus st = OTA_DL_IDLE;
    int pct = -1;
    String err = "";
    getDownloadState(st, pct, err);
    return st == OTA_DL_RUNNING;
}

void OtaManager::setDownloadState(OtaDownloadStatus st, int pct, const String &err)
{
    if (otaStateMutex != nullptr)
        xSemaphoreTake(otaStateMutex, portMAX_DELAY);
    downloadStatus = st;
    downloadProgress = pct;
    downloadError = err;
    if (otaStateMutex != nullptr)
        xSemaphoreGive(otaStateMutex);
}

void OtaManager::getDownloadState(OtaDownloadStatus &st, int &pct, String &err)
{
    if (otaStateMutex != nullptr)
        xSemaphoreTake(otaStateMutex, portMAX_DELAY);
    st = downloadStatus;
    pct = downloadProgress;
    err = downloadError;
    if (otaStateMutex != nullptr)
        xSemaphoreGive(otaStateMutex);
}

void OtaManager::onDownloadProgress(size_t written, int contentLength)
{
    downloadBytes += written;
    int pct = -1;
    if (contentLength > 0)
        pct = (int)((downloadBytes * 100) / contentLength);
    setDownloadState(OTA_DL_RUNNING, pct, "");
}

void OtaManager::otaTaskEntry(void *arg)
{
    if (arg == nullptr)
        vTaskDelete(nullptr);
    ((OtaManager *)arg)->otaTaskRun();
}

void OtaManager::otaTaskRun()
{
    String err = "";
    bool success = false;
    String dlUrl = activeDownloadUrl;

    HTTPClient http;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    http.setTimeout(15000);

    do
    {
        bool beginOk = false;
        if (dlUrl.startsWith("https://"))
        {
            secureClient.setInsecure();
            beginOk = http.begin(secureClient, dlUrl);
        }
        else
        {
            beginOk = http.begin(plainClient, dlUrl);
        }
        if (!beginOk)
        {
            err = "HTTP begin failed";
            break;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            err = "HTTP GET " + String(code);
            http.end();
            break;
        }

        int contentLength = http.getSize();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            err = "Update.begin err=" + String(Update.getError());
            http.end();
            break;
        }

        OtaUpdateStream stream(this, contentLength);
        int written = http.writeToStream(&stream);
        if (written < 0)
        {
            err = "HTTP stream error";
            Update.abort();
            http.end();
            break;
        }

        if (!Update.end(true))
        {
            err = "Update.end err=" + String(Update.getError());
            http.end();
            break;
        }

        http.end();
        success = true;
        setDownloadState(OTA_DL_DONE, 100, "");
    } while (false);

    if (otaStateMutex != nullptr)
    {
        xSemaphoreTake(otaStateMutex, portMAX_DELAY);
        otaTaskRunning = false;
        xSemaphoreGive(otaStateMutex);
    }

    if (!success)
        setDownloadState(OTA_DL_ERROR, -1, err);

    otaTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

bool OtaManager::fetchMetadata(String &remoteVersion, String &remoteUrl, String &errMsg)
{
    // 版本检查必须尽快返回，否则主循环不处理 MQTT 心跳会被 Broker 踢下线。
    // 只请求一个当前清单，避免 MQTT 回调里串行访问多个 HTTPS 源触发看门狗。
    for (size_t baseIndex = 0; baseIndex < sizeof(OTA_MANIFEST_BASES) / sizeof(OTA_MANIFEST_BASES[0]); baseIndex++)
    {
        String metaUrl = otaManifestUrl(OTA_MANIFEST_BASES[baseIndex]);
        // jsDelivr/Fastly 可能缓存 master 清单，追加时间戳确保每次检查拿到最新版本。
        metaUrl += (metaUrl.indexOf('?') >= 0 ? "&" : "?");
        metaUrl += "t=" + String(millis());
        DBG("[OTA] 读取版本清单 %s\n", metaUrl.c_str());

        for (int attempt = 1; attempt <= 1; attempt++)
        {
            DBG("[OTA] 清单第 %d 次尝试\n", attempt);
            bool isHttps = metaUrl.startsWith("https://");
            WiFiClient plainClient;
            WiFiClientSecure secureClient;
            HTTPClient http;
            http.setTimeout(2500);
            secureClient.setTimeout(2500);
            http.useHTTP10(true);
            http.addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            http.addHeader("Pragma", "no-cache");
            bool beginOk;
            if (isHttps)
            {
                secureClient.setInsecure();
                beginOk = http.begin(secureClient, metaUrl);
            }
            else
                beginOk = http.begin(plainClient, metaUrl);
            if (!beginOk)
            {
                errMsg = "meta HTTP begin failed";
                continue;
            }

            int code = http.GET();
            if (code != HTTP_CODE_OK)
            {
                errMsg = "meta HTTP " + String(code);
                http.end();
                continue;
            }

            int metaSize = http.getSize();
            if (metaSize > 8192)
            {
                errMsg = "meta 响应过大";
                http.end();
                continue;
            }

            String body;
            if (!fetchText(http, body))
            {
                errMsg = "meta JSON 读取失败";
                http.end();
                continue;
            }

            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok)
            {
                errMsg = "meta JSON 解析失败";
                http.end();
                continue;
            }

            remoteVersion = "";
            remoteUrl = "";
            extractManifestString(doc, "versions", OTA_ENV_NAME, remoteVersion);
            if (!remoteVersion.length())
                extractManifestString(doc, "version", OTA_ENV_NAME, remoteVersion);
            extractManifestString(doc, "urls", OTA_ENV_NAME, remoteUrl);
            if (!remoteUrl.length())
                extractManifestString(doc, "url", OTA_ENV_NAME, remoteUrl);

            http.end();
            if (remoteVersion.length() == 0)
            {
                errMsg = "meta 缺少 version 字段";
                continue;
            }
            if (remoteUrl.length() == 0)
                remoteUrl = otaDefaultUrl();
            return true;
        }
    }
    return false;
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
