#pragma once

#include <Arduino.h>
#include <Update.h>
#include <functional>

typedef enum {
    OTA_AUTH_ERROR,
    OTA_BEGIN_ERROR,
    OTA_CONNECT_ERROR,
    OTA_RECEIVE_ERROR,
    OTA_END_ERROR
} ota_error_t;

class ArduinoOTAClass
{
public:
    using StartHandler = std::function<void(void)>;
    using ErrorHandler = std::function<void(ota_error_t)>;
    using ProgressHandler = std::function<void(unsigned int, unsigned int)>;

    ArduinoOTAClass &setHostname(const char *) { return *this; }
    ArduinoOTAClass &setPasswordHash(const char *) { return *this; }
    ArduinoOTAClass &onStart(StartHandler) { return *this; }
    ArduinoOTAClass &onEnd(StartHandler) { return *this; }
    ArduinoOTAClass &onError(ErrorHandler) { return *this; }
    ArduinoOTAClass &onProgress(ProgressHandler) { return *this; }
    void begin() {}
    void handle() {}
    int getCommand() const { return U_FLASH; }
};

inline ArduinoOTAClass ArduinoOTA;
