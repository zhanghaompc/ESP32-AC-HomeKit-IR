#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class HTTPClient;
class WiFiClient;
class WiFiClientSecure;

// 检查更新结果（与 HTTPUpdate 枚举数值保持一致，避免跨头文件依赖）
enum OtaCheckResult
{
    OTA_CHECK_FAILED = 0,     // 检查/下载失败
    OTA_CHECK_NO_UPDATE = 1,  // 已是最新版本，无需升级
    OTA_CHECK_OK = 2          // 发现新版本 / 已下载并写入新固件
};

// 异步下载状态（配合主循环分片读取，可上报进度）
enum OtaDownloadStatus
{
    OTA_DL_IDLE = 0,    // 未在下载
    OTA_DL_RUNNING = 1, // 下载中
    OTA_DL_DONE = 2,    // 下载并写入完成，可重启
    OTA_DL_ERROR = 3    // 出错
};

class OtaManager
{
public:
    void begin();                // 加载配置
    String getUrl() const;
    String getVersion() const;
    String getPendingUrl() const;
    bool setUrl(const String &url);
    int checkUpdate(String &errMsg);        // 阻塞式：比较版本后下载升级（网页/BLE 用）
    int checkForUpdate(String &remoteVersion, String &errMsg); // 只查版本，不下载（MQTT 确认流程用）
    bool beginDownload(const String &downloadUrl, String &errMsg); // 开始异步下载
    int processDownload(String &errMsg, int &progressPercent);    // 主循环分片驱动，返回 OtaDownloadStatus
    bool isDownloading();
    void onDownloadProgress(size_t written, int contentLength);

private:
    String url = "";
    String pendingUrl = "";    // checkForUpdate 找到的待下载地址
    String activeDownloadUrl = "";
    TaskHandle_t otaTaskHandle = nullptr;
    SemaphoreHandle_t otaStateMutex = nullptr;
    volatile OtaDownloadStatus downloadStatus = OTA_DL_IDLE;
    volatile int downloadProgress = -1;
    String downloadError = "";
    bool otaTaskRunning = false;
    unsigned long downloadBytes = 0;

    void saveConfig();
    bool fetchMetadata(String &remoteVersion, String &remoteUrl, String &errMsg);
    bool isVersionNewer(const String &remote, const String &current);
    void setDownloadState(OtaDownloadStatus st, int pct, const String &err);
    void getDownloadState(OtaDownloadStatus &st, int &pct, String &err);
    static void otaTaskEntry(void *arg);
    void otaTaskRun();
};

extern OtaManager otaManager;
