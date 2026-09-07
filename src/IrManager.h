#pragma once
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRrecv.h>
#include <IRutils.h>

class IrManager
{
public:
    IrManager();
    void begin();
    void loop();
    void send(int temp, int speed, int mode, bool power = true);
    String learnProtocol();
    uint32_t requestCount() const;
    uint32_t transmitCount() const;
    uint32_t overwriteCount() const;
    uint32_t taskStackFreeBytes() const;

private:
    IRrecv *irrecv = nullptr;
};
