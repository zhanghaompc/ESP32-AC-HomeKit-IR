#include "BleManager.h"
#include "LedManager.h"
#include "IrManager.h"
#include "TimerManager.h"
#include <Arduino.h>
#include <FastLED.h>
#include <IRremoteESP8266.h>
#include <IRac.h>

extern float envTemperature;
extern float enHumidity;
extern String lastProtocolName;
extern LedManager ledManager;
extern IrManager irManager;
extern TimerManager timerManager;
extern bool requestSwitchToWiFi;
extern bool isBLEMode;
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
    if (abs(temp - lastSentTemp) < 0.1f && abs(humidity - lastSentHumidity) < 0.3f)
        return;

    if (!takeMutex())
        return;
    char buffer[15];
    snprintf(buffer, sizeof(buffer), "t%.1fh%.1f", temp, humidity);
    pTxCharacteristic->setValue(buffer);
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

    // 查询状态 - 只返回温湿度（新格式）
    if (command == "status")
    {
        char buffer[15];
        snprintf(buffer, sizeof(buffer), "t%.1fh%.1f", envTemperature, enHumidity);
        pTxCharacteristic->setValue(buffer);
        pTxCharacteristic->notify();
        giveMutex();
        return;
    }

    // 查询电源状态
    if (command == "power")
    {
        String s = String("power=") + (ac.next.power ? "on" : "off");
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

    // 获取当前时间
    if (command == "time")
    {
        String s = "time=" + timerManager.getCurrentTime();
        pTxCharacteristic->setValue(s.c_str());
        pTxCharacteristic->notify();
        giveMutex();
        return;
    }

    // 定时任务处理
    if (command.startsWith("timer="))
    {
        String timerCmd = command.substring(6);

        // 查询定时任务列表
        if (timerCmd == "list")
        {
            String s = "timers=" + timerManager.getTaskList();
            pTxCharacteristic->setValue(s.c_str());
            pTxCharacteristic->notify();
            giveMutex();
            return;
        }

        // 添加定时任务 timer=add;hour=8;minute=0;temp=26;mode=1;speed=2;power=on;repeat=1
        if (timerCmd.startsWith("add;"))
        {
            int hour = 0, minute = 0, temp = 25, mode = 0, speed = 0;
            bool power = true, repeat = false;

            int pos = 4;
            while (pos < timerCmd.length())
            {
                int semi = timerCmd.indexOf(';', pos);
                if (semi == -1)
                    semi = timerCmd.length();
                String part = timerCmd.substring(pos, semi);

                if (part.startsWith("hour="))
                    hour = part.substring(5).toInt();
                else if (part.startsWith("minute="))
                    minute = part.substring(7).toInt();
                else if (part.startsWith("temp="))
                    temp = part.substring(5).toInt();
                else if (part.startsWith("mode="))
                    mode = part.substring(5).toInt();
                else if (part.startsWith("speed="))
                    speed = part.substring(6).toInt();
                else if (part.startsWith("power="))
                    power = (part.substring(6) == "on");
                else if (part.startsWith("repeat="))
                    repeat = (part.substring(7) == "1");

                pos = semi + 1;
            }

            int id = timerManager.addTask(hour, minute, temp, mode, speed, power, repeat);
            String s = (id >= 0) ? String("timer_add=success;id=") + id : "timer_add=failed";
            pTxCharacteristic->setValue(s.c_str());
            pTxCharacteristic->notify();
            giveMutex();
            return;
        }

        // 删除定时任务 timer=delete;id=1
        if (timerCmd.startsWith("delete;id="))
        {
            int id = timerCmd.substring(10).toInt();
            bool ok = timerManager.deleteTask(id);
            String s = ok ? String("timer_delete=success;id=") + id : "timer_delete=failed";
            pTxCharacteristic->setValue(s.c_str());
            pTxCharacteristic->notify();
            giveMutex();
            return;
        }

        // 启用/禁用定时任务 timer=enable;id=1;state=1
        if (timerCmd.startsWith("enable;id="))
        {
            int idPos = timerCmd.indexOf(";id=") + 4;
            int statePos = timerCmd.indexOf(";state=");
            int id = timerCmd.substring(idPos, statePos).toInt();
            bool state = (timerCmd.substring(statePos + 7) == "1");
            bool ok = timerManager.enableTask(id, state);
            String s = ok ? String("timer_enable=success;id=") + id : "timer_enable=failed";
            pTxCharacteristic->setValue(s.c_str());
            pTxCharacteristic->notify();
            giveMutex();
            return;
        }
    }

    // 切换到WiFi模式
    if (command == "wifi_mode")
    {
        if (isBLEMode)
        {
            requestSwitchToWiFi = true;
            pTxCharacteristic->setValue("switching=wifi");
            pTxCharacteristic->notify();
            Serial.println("收到切换WiFi模式命令");
        }
        else
        {
            pTxCharacteristic->setValue("already=wifi_mode");
            pTxCharacteristic->notify();
        }
        giveMutex();
        return;
    }

    // 未知命令
    String unknown = String("unknown_cmd:") + command;
    pTxCharacteristic->setValue(unknown.c_str());
    pTxCharacteristic->notify();
    giveMutex();
}