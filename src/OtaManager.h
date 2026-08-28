#pragma once
#include <Arduino.h>

class OtaManager
{
public:
    void begin();                // 加载配置
    String getUrl() const;
    String getVersion() const;
    bool setUrl(const String &url);
    int checkUpdate(String &errMsg);   // 从 URL 下载并升级，失败时 errMsg 带原因

private:
    String url = "";
    void saveConfig();
};

extern OtaManager otaManager;
