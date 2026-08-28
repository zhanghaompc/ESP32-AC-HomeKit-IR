#pragma once
#include <Arduino.h>

// 检查更新结果（与 HTTPUpdate 枚举数值保持一致，避免跨头文件依赖）
enum OtaCheckResult
{
    OTA_CHECK_FAILED = 0,     // 检查/下载失败
    OTA_CHECK_NO_UPDATE = 1,  // 已是最新版本，无需升级
    OTA_CHECK_OK = 2          // 已下载并写入新固件
};

class OtaManager
{
public:
    void begin();                // 加载配置
    String getUrl() const;
    String getVersion() const;
    bool setUrl(const String &url);
    int checkUpdate(String &errMsg);   // 先比较版本，有新版本才下载升级

private:
    String url = "";
    void saveConfig();
    bool fetchMetadata(String &remoteVersion, String &remoteUrl, String &errMsg);
    bool isVersionNewer(const String &remote, const String &current);
};

extern OtaManager otaManager;
