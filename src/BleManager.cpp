#include "BleManager.h"
#include "LedManager.h"
#include "IrManager.h"
#include "TimerManager.h"
#include "Debug.h"
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
extern bool requestFactoryReset;
extern bool isBLEMode;
extern IRrecv irrecv;
bool updateProtocolFromString(const String &, decode_type_t &);
extern IRac ac;
String handleIrReceiving();
extern bool irReceiverEnabled;
void irEnableRecv();
void irDisableRecv();
extern volatile bool irLearning;

#ifndef WIFI_ONLY
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
        ledManager.setColor(CRGB::Cyan); // BLE 已连接 = 青色常亮
        pServer->getAdvertising()->stop(); // 连接后停止广播，省电且更规范
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
#endif

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
#ifndef WIFI_ONLY
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
#endif
}

void BleManager::disable()
{
#ifndef WIFI_ONLY
    if (pServer)
        BLEDevice::deinit();
    pServer = nullptr;
    pTxCharacteristic = nullptr;
    deviceConnected = false;
    ledManager.stopBlink();
    ledManager.off();
#endif
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
#ifndef WIFI_ONLY
    if (!takeMutex())
        return;
    if (deviceConnected && pTxCharacteristic)
    {
        pTxCharacteristic->setValue(status.c_str());
        pTxCharacteristic->notify();
    }
    giveMutex();
#endif
}

void BleManager::sendTempHumidity(float temp, float humidity)
{
#ifndef WIFI_ONLY
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
#endif
}

void BleManager::sendProtocol(const String &protocol)
{
#ifndef WIFI_ONLY
    if (!deviceConnected || !pTxCharacteristic)
        return;

    if (!takeMutex())
        return;
    String data = "protocol=" + protocol;
    pTxCharacteristic->setValue(data.c_str());
    pTxCharacteristic->notify();
    giveMutex();
#endif
}

// 分块发送：BLE 默认单包最多 20 字节，超长消息会被截断。
// 每片 18 字节，块与块之间留 10ms，最后追加 '\n' 作为完整消息结束标记。
// 注意：调用前必须已持有 xBleMutex（由 handleCommand 内的调用保证）。
void BleManager::sendChunked(const String &data)
{
    if (responseForwarder)
        responseForwarder(data);
#ifndef WIFI_ONLY
    if (!deviceConnected || !pTxCharacteristic)
        return;

    const size_t chunkSize = 18;
    for (size_t i = 0; i < data.length(); i += chunkSize)
    {
        String chunk = data.substring(i, i + chunkSize);
        pTxCharacteristic->setValue(chunk.c_str());
        pTxCharacteristic->notify();
        delay(10);
    }
    pTxCharacteristic->setValue("\n");
    pTxCharacteristic->notify();
#endif
}

#ifndef WIFI_ONLY
void BleManager::startAdvertising()
{
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    // 空闲广播间隔约 1~1.5 秒（单位 0.625ms），降低空闲功耗
    adv->setMinInterval(1600);
    adv->setMaxInterval(2400);
    adv->start();
}
#endif

// ====================== 核心指令处理（线程安全+匹配小程序） ======================
void BleManager::handleCommand(const String &command)
{
    DBG("命令: %s\n", command.c_str());
    if (!takeMutex())
        return;

    // 关机
    if (command == "power=off")
    {
        ac.next.power = false;
        irManager.send(ac.next.degrees, (int)ac.next.fanspeed, (int)ac.next.mode, false);
        sendChunked("power=off");
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
        int speed = command.substring(idx2 + 7, idx3).toInt();

        ac.next.power = true;
        irManager.send(temp, speed, mode, true);

        String resp = String("temp=") + temp + ";mode=" + mode + ";speed=" + speed + ";power=on";
        sendChunked(resp);
        giveMutex();
        return;
    }

    // 附加功能：强劲/扫风/面板灯/睡眠/自清洁（切换型）
    if (command == "turbo")
    {
        ac.next.turbo = !ac.next.turbo;
        irManager.send(ac.next.degrees, (int)ac.next.fanspeed, (int)ac.next.mode, ac.next.power);
        sendChunked(String("turbo=") + (ac.next.turbo ? "on" : "off"));
        giveMutex();
        return;
    }
    if (command == "swing")
    {
        ac.next.swingv = (ac.next.swingv == stdAc::swingv_t::kOff) ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;
        irManager.send(ac.next.degrees, (int)ac.next.fanspeed, (int)ac.next.mode, ac.next.power);
        sendChunked(String("swing=") + (ac.next.swingv != stdAc::swingv_t::kOff ? "on" : "off"));
        giveMutex();
        return;
    }
    if (command == "light")
    {
        ac.next.light = !ac.next.light;
        irManager.send(ac.next.degrees, (int)ac.next.fanspeed, (int)ac.next.mode, ac.next.power);
        sendChunked(String("light=") + (ac.next.light ? "on" : "off"));
        giveMutex();
        return;
    }
    if (command == "sleep")
    {
        ac.next.sleep = (ac.next.sleep < 0) ? 1 : -1;
        irManager.send(ac.next.degrees, (int)ac.next.fanspeed, (int)ac.next.mode, ac.next.power);
        sendChunked(String("sleep=") + (ac.next.sleep >= 0 ? "on" : "off"));
        giveMutex();
        return;
    }
    if (command == "clean")
    {
        ac.next.clean = !ac.next.clean;
        irManager.send(ac.next.degrees, (int)ac.next.fanspeed, (int)ac.next.mode, ac.next.power);
        sendChunked(String("clean=") + (ac.next.clean ? "on" : "off"));
        giveMutex();
        return;
    }

    // 协议设置
    if (command.startsWith("protocol="))
    {
        String proto = command.substring(9);
        bool ok = updateProtocolFromString(proto, ac.next.protocol);
        String res = ok ? String("protocol=") + proto : "protocol=invalid";
        sendChunked(res);
        giveMutex();
        return;
    }

    // 学习模式
    if (command == "learn=start")
    {
        sendChunked("learn=waiting");
        giveMutex();

        ledManager.stopBlink();
        ledManager.setColor(CRGB::Purple);
        irLearning = true; // 暂停主循环红外解析，数据只给学习流程
        bool wasRecvEnabled = irReceiverEnabled; // 记录学习前状态，学习后恢复
        irEnableRecv(); // 学习期间开启红外接收（已开启则跳过，避免重复初始化）
        unsigned long start = millis();
        String proto = "";

        while (proto.isEmpty() && millis() - start < 10000)
        {
            String p = handleIrReceiving();
            // UNKNOWN 是噪声/半截帧，不算识别成功，继续等待真正的协议
            if (!p.isEmpty() && p != "UNKNOWN") proto = p;
            delay(100);
        }

        irLearning = false;
        if (!wasRecvEnabled) irDisableRecv(); // 学习前未开启则恢复关闭
        ledManager.off();
        if (deviceConnected)
            ledManager.setColor(CRGB::Blue);

        takeMutex();
        if (!proto.isEmpty() && proto != "UNKNOWN")
        {
            updateProtocolFromString(proto, ac.next.protocol);
            String res = String("learn=success:") + proto;
            sendChunked(res);
        }
        else
        {
            sendChunked("learn=timeout");
        }
        giveMutex();
        return;
    }

    // 查询状态 - 只返回温湿度（新格式）
    if (command == "status")
    {
        char buffer[15];
        snprintf(buffer, sizeof(buffer), "t%.1fh%.1f", envTemperature, enHumidity);
        sendChunked(buffer);
        giveMutex();
        return;
    }

    // 查询电源状态
    if (command == "power")
    {
        String s = String("power=") + (ac.next.power ? "on" : "off");
        sendChunked(s);
        giveMutex();
        return;
    }

    // 获取当前协议
    if (command == "get_protocol")
    {
        String s = "protocol=" + lastProtocolName;
        sendChunked(s);
        giveMutex();
        return;
    }

    // 手机校时 time=YYYY-MM-DD HH:MM:SS
    if (command.startsWith("time="))
    {
        bool ok = timerManager.setTimeFromPhone(command.substring(5));
        String res = ok ? "time=ok" : "time=invalid";
        sendChunked(res);
        giveMutex();
        return;
    }

    // 获取当前时间
    if (command == "time")
    {
        String s = "time=" + timerManager.getCurrentTime();
        sendChunked(s);
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
            sendChunked(s);
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
            sendChunked(s);
            giveMutex();
            return;
        }

        // 更新定时任务 timer=update;id=1;hour=8;minute=0;temp=26;mode=1;speed=2;power=on;repeat=1
        if (timerCmd.startsWith("update;"))
        {
            int id = 0, hour = 0, minute = 0, temp = 25, mode = 0, speed = 0;
            bool power = true, repeat = false;

            int pos = 7; // "update;" 长度
            while (pos < timerCmd.length())
            {
                int semi = timerCmd.indexOf(';', pos);
                if (semi == -1)
                    semi = timerCmd.length();
                String part = timerCmd.substring(pos, semi);

                if (part.startsWith("id="))
                    id = part.substring(3).toInt();
                else if (part.startsWith("hour="))
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

            bool ok = timerManager.updateTask(id, hour, minute, temp, mode, speed, power, repeat);
            String s = ok ? String("timer_update=success;id=") + id : "timer_update=failed";
            sendChunked(s);
            giveMutex();
            return;
        }

        // 删除定时任务 timer=delete;id=1
        if (timerCmd.startsWith("delete;id="))
        {
            int id = timerCmd.substring(10).toInt();
            bool ok = timerManager.deleteTask(id);
            String s = ok ? String("timer_delete=success;id=") + id : "timer_delete=failed";
            sendChunked(s);
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
            sendChunked(s);
            giveMutex();
            return;
        }
    }

    // 切换到WiFi模式
    if (command == "wifi_mode")
    {
#ifdef BLE_ONLY
        // 纯BLE版本不支持WiFi模式
        sendChunked("wifi=unsupported");
        giveMutex();
        return;
#else
        if (isBLEMode)
        {
            requestSwitchToWiFi = true;
            sendChunked("switching=wifi");
            Serial.println("收到切换WiFi模式命令");
        }
        else
        {
            sendChunked("already=wifi_mode");
        }
        giveMutex();
        return;
#endif
    }

    // 恢复出厂设置（清除 HomeKit 配对 / WiFi / 定时任务）
    if (command == "reset_factory")
    {
        sendChunked("reset=ok");
        giveMutex();
        requestFactoryReset = true;
        return;
    }

    // 未知命令
    String unknown = String("unknown_cmd:") + command;
    sendChunked(unknown);
    giveMutex();
}
