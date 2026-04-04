#include "BleManager.h"
#include "LedManager.h"
#include "IrManager.h"
#include <Arduino.h>
#include <FastLED.h>
#include <IRremoteESP8266.h>
#include <IRac.h>

extern float envTemperature;
extern float enHumidity;
extern String lastProtocolName;
extern LedManager ledManager;
extern IrManager irManager;
bool updateProtocolFromString(const String &, decode_type_t &);
extern IRac ac;
String handleIrReceiving();

// BLE连接回调
class InternalBLEServerCallbacks : public BLEServerCallbacks
{
    BleManager *parent;

public:
    InternalBLEServerCallbacks(BleManager *mgr) : parent(mgr) {}
    void onConnect(BLEServer *pServer) override
    {
        parent->deviceConnected = true;
        Serial.println("BLE客户端已连接");
        ledManager.stopBlink();
        ledManager.setColor(CRGB::Blue);
        parent->sendTempHumidity(envTemperature, enHumidity);
        parent->sendProtocol(lastProtocolName);
    }
    void onDisconnect(BLEServer *pServer) override
    {
        parent->deviceConnected = false;
        Serial.println("BLE客户端已断开");
        pServer->startAdvertising();
        ledManager.blinkBlue();
    }
};

// BLE接收回调
class InternalBLECharacteristicCallbacks : public BLECharacteristicCallbacks
{
    BleManager *parent;

public:
    InternalBLECharacteristicCallbacks(BleManager *mgr) : parent(mgr) {}
    void onWrite(BLECharacteristic *pCharacteristic) override
    {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.empty())
            return;
        parent->handleCommand(String(rxValue.c_str()));
    }
};

// 互斥锁操作
bool BleManager::takeMutex()
{
    return (xBleMutex != nullptr) ? (xSemaphoreTake(xBleMutex, xMutexTimeout) == pdTRUE) : false;
}
void BleManager::giveMutex()
{
    if (xBleMutex != nullptr)
        xSemaphoreGive(xBleMutex);
}

BleManager::BleManager() {}

void BleManager::begin()
{
    if (xBleMutex == nullptr)
    {
        xBleMutex = xSemaphoreCreateMutex();
    }
}

void BleManager::enable()
{
    if (pServer)
    {
        BLEDevice::deinit();
        pServer = nullptr;
        pTxCharacteristic = nullptr;
        delay(500);
    }

    BLEDevice::init("ESP32-AC");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new InternalBLEServerCallbacks(this));

    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic *pRx = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRx->setCallbacks(new InternalBLECharacteristicCallbacks(this));

    pService->start();
    startAdvertising();
    ledManager.blinkBlue();
}

void BleManager::disable()
{
    if (pServer)
        BLEDevice::deinit();
    pServer = nullptr;
    pTxCharacteristic = nullptr;
    deviceConnected = false;
    ledManager.stopBlink();
    ledManager.off();
}

void BleManager::loop()
{
    if (!deviceConnected && oldDeviceConnected)
    {
        delay(500);
        oldDeviceConnected = false;
    }
    if (deviceConnected && !oldDeviceConnected)
    {
        oldDeviceConnected = true;
    }
}

bool BleManager::isConnected() const { return deviceConnected; }

void BleManager::sendStatus(const String &status)
{
    if (!takeMutex())
        return;
    if (deviceConnected && pTxCharacteristic)
    {
        pTxCharacteristic->setValue(status.c_str());
        pTxCharacteristic->notify();
    }
    giveMutex();
}

void BleManager::sendTempHumidity(float temp, float humidity)
{
    if (!deviceConnected || !pTxCharacteristic)
        return;
    if (abs(temp - lastSentTemp) < 0.2f && abs(humidity - lastSentHumidity) < 0.5f)
        return;

    if (!takeMutex())
        return;
    String data = "temp=" + String(temp, 1) + ";humidity=" + String(humidity, 1);
    pTxCharacteristic->setValue(data.c_str());
    pTxCharacteristic->notify();
    lastSentTemp = temp;
    lastSentHumidity = humidity;
    giveMutex();
}

void BleManager::sendProtocol(const String &protocol)
{
    if (!deviceConnected || !pTxCharacteristic)
        return;

    if (!takeMutex())
        return;
    String data = "protocol=" + protocol;
    pTxCharacteristic->setValue(data.c_str());
    pTxCharacteristic->notify();
    giveMutex();
}

void BleManager::startAdvertising()
{
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->start();
}

// ====================== 核心指令处理（线程安全+匹配小程序） ======================
void BleManager::handleCommand(const String &command)
{
    Serial.printf("BLE命令: %s\n", command.c_str());
    if (!pTxCharacteristic || !takeMutex())
        return;

    // 关机
    if (command == "power=off")
    {
        ac.next.power = false;
        irManager.send(ac.next.degrees, (int)ac.next.fanspeed, (int)ac.next.mode, false);
        pTxCharacteristic->setValue("power=off");
        pTxCharacteristic->notify();
        giveMutex();
        return;
    }

    // 开机/调温/调模式/调风速
    if (command.startsWith("temp="))
    {
        int idx1 = command.indexOf(';');
        int idx2 = command.indexOf(';', idx1 + 1);
        int idx3 = command.indexOf(';', idx2 + 1);

        int temp = command.substring(5, idx1).toInt();
        int mode = command.substring(idx1 + 6, idx2).toInt();
        int speed = command.substring(idx2 + 6, idx3).toInt();

        ac.next.power = true;
        irManager.send(temp, speed, mode, true);

        String resp = String("temp=") + temp + ";mode=" + mode + ";speed=" + speed + ";power=on";
        pTxCharacteristic->setValue(resp.c_str());
        pTxCharacteristic->notify();
        giveMutex();
        return;
    }

    // 协议设置
    if (command.startsWith("protocol="))
    {
        String proto = command.substring(9);
        bool ok = updateProtocolFromString(proto, ac.next.protocol);
        String res = ok ? String("protocol=") + proto : "protocol=invalid";
        pTxCharacteristic->setValue(res.c_str());
        pTxCharacteristic->notify();
        giveMutex();
        return;
    }

    // 学习模式
    if (command == "learn=start")
    {
        pTxCharacteristic->setValue("learn=waiting");
        pTxCharacteristic->notify();
        giveMutex();

        ledManager.stopBlink();
        ledManager.setColor(CRGB::Purple);
        unsigned long start = millis();
        String proto = "";

        while (proto.isEmpty() && millis() - start < 10000)
        {
            proto = handleIrReceiving();
            delay(100);
        }

        ledManager.off();
        if (deviceConnected)
            ledManager.setColor(CRGB::Blue);

        takeMutex();
        if (!proto.isEmpty())
        {
            updateProtocolFromString(proto, ac.next.protocol);
            String res = String("learn=success:") + proto;
            pTxCharacteristic->setValue(res.c_str());
        }
        else
        {
            pTxCharacteristic->setValue("learn=timeout");
        }
        pTxCharacteristic->notify();
        giveMutex();
        return;
    }

    // 查询状态
    if (command == "status")
    {
        String s = "temp=" + String(envTemperature, 1) + ";humidity=" + String(enHumidity, 1) + ";power=" + String(ac.next.power ? "on" : "off") + ";protocol=" + lastProtocolName;
        pTxCharacteristic->setValue(s.c_str());
        pTxCharacteristic->notify();
        giveMutex();
        return;
    }

    // 获取当前协议
    if (command == "get_protocol")
    {
        String s = "protocol=" + lastProtocolName;
        pTxCharacteristic->setValue(s.c_str());
        pTxCharacteristic->notify();
        giveMutex();
        return;
    }

    // 未知命令
    String unknown = String("unknown_cmd:") + command;
    pTxCharacteristic->setValue(unknown.c_str());
    pTxCharacteristic->notify();
    giveMutex();
}