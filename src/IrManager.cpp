#include "IrManager.h"
#include "LedManager.h"
#include "BleManager.h"
#include "Debug.h"
#include <FastLED.h>
#include <IRremoteESP8266.h>
#include <IRac.h>

extern LedManager ledManager;
extern String lastProtocolName;
extern IRac ac;
extern bool isBLEMode;
extern bool isWiFiMode;
extern BleManager bleManager;
bool updateProtocolFromString(const String &protocolName, decode_type_t &targetProtocol);

static int pendingTemp = 25;
static int pendingSpeed = 2;
static int pendingMode = 1;
static bool pendingPower = true;
static volatile bool irSendPending = false;
static TaskHandle_t irTaskHandle = NULL;
static portMUX_TYPE irMux = portMUX_INITIALIZER_UNLOCKED;

void irTaskFunction(void *parameter)
{
    int temp, speed, mode;
    bool power;

    while (true)
    {
        if (irSendPending)
        {
            portENTER_CRITICAL(&irMux);
            temp = pendingTemp;
            speed = pendingSpeed;
            mode = pendingMode;
            power = pendingPower;
            irSendPending = false;
            portEXIT_CRITICAL(&irMux);

            ledManager.stopBlink();
            ledManager.setColor(CRGB::Red);

            updateProtocolFromString(lastProtocolName, ac.next.protocol);
            ac.next.degrees = temp;
            ac.next.fanspeed = (stdAc::fanspeed_t)speed;
            ac.next.mode = (stdAc::opmode_t)mode;
            ac.next.power = power;
            ac.next.light = true;
            ac.next.beep = false;
            ac.next.econo = false;
            ac.next.filter = false;
            ac.next.turbo = false;
            ac.next.quiet = false;
            ac.next.sleep = -1;
            ac.next.clean = false;
            ac.next.clock = -1;

            DBG("准备发射红外信号...\n");
            DBG("当前空调协议: %s\n", lastProtocolName.c_str());
            DBG("待发射参数 - 温度: %d, 风速: %d, 模式: %d, 电源: %s\n", temp, speed, mode, power ? "开启" : "关闭");

            ac.sendAc();
            delay(100);

            ledManager.off();

            if (isBLEMode)
            {
                if (bleManager.isConnected())
                {
                    ledManager.setColor(CRGB::Blue);
                }
                else
                {
                    // 未连接时恢复“等待连接”的蓝色闪烁，避免看起来像蓝牙已关闭
                    ledManager.blinkBlue();
                }
            }

            DBG("当前空调的协议: %s\n", lastProtocolName.c_str());
            DBG("设置温度: %d, 风速: %d, 模式: %d, 电源: %s(运行核心: %d)\n", temp, speed, mode, power ? "开启" : "关闭", xPortGetCoreID());
        }
        delay(10);
    }
}

IrManager::IrManager() {}

void IrManager::begin()
{
    xTaskCreatePinnedToCore(
        irTaskFunction,
        "IR_Tx_Task",
        4096 + 1028,
        NULL,
        1,
        &irTaskHandle,
        1);
    Serial.println("红外发射任务已启动");
}

void IrManager::loop() {}

void IrManager::send(int temp, int speed, int mode, bool power)
{
    portENTER_CRITICAL(&irMux);
    pendingTemp = temp;
    pendingSpeed = speed;
    pendingMode = mode;
    pendingPower = power;
    irSendPending = true;
    portEXIT_CRITICAL(&irMux);
    DBG("已请求红外发射: 温度:%d℃, 风速:%d, 模式:%d, 电源:%s\n", temp, speed, mode, power ? "开启" : "关闭");
}

String IrManager::learnProtocol() { return ""; }
