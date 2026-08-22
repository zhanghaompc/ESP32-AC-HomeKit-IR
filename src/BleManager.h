#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h> 
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <FastLED.h>

#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class BleManager
{
private:
    BLEServer *pServer = nullptr;
    BLECharacteristic *pTxCharacteristic = nullptr;
    bool oldDeviceConnected = false;
    float lastSentTemp = 0.0f;
    float lastSentHumidity = 0.0f;

    SemaphoreHandle_t xBleMutex = nullptr;
    const TickType_t xMutexTimeout = pdMS_TO_TICKS(100);

    void startAdvertising();
    bool takeMutex();
    void giveMutex();

public:
    // 公开变量，彻底解决权限报错
    bool deviceConnected = false;

    BleManager();
    void begin();
    void enable();
    void disable();
    void loop();
    bool isConnected() const;
    void sendStatus(const String &status);
    void sendChunked(const String &data);
    void sendTempHumidity(float temp, float humidity);
    void sendProtocol(const String &protocol);
    void handleCommand(const String &command);
};

extern BleManager bleManager;

#endif
