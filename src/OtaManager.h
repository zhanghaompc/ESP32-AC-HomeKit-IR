#pragma once
#include <Arduino.h>

class OtaManager
{
public:
    void begin();                // 加载配置
    String getUrl() const;
    String getVersion() const;
    bool setUrl(const String &url);
    int checkUpdate();           // 从 URL 下载并升级，返回 t_httpUpdate_return

private:
    String url = "";
    void saveConfig();
};

extern OtaManager otaManager;
