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

static int pendingTemp = 25;
static int pendingSpeed = 2;
static int pendingMode = 1;
static bool pendingPower = true;
static volatile bool irSendPending = false;
static volatile uint32_t irRequestCount = 0;
static volatile uint32_t irTransmitCount = 0;
static volatile uint32_t irOverwriteCount = 0;
// 调温滑块会在一次拖动中产生很多中间值。连续发送完整红外帧会让
// 空调接收器丢帧，因此等待最后一次请求稳定一小段时间后再发射。
static volatile unsigned long irLastRequestMs = 0;
static const unsigned long IR_DEBOUNCE_MS = 180;
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
            // 合并短时间内连续到达的温度/模式更新，只发送最后一帧。
            // 读取时间放在临界区外也安全：写入方只会单调更新该时间戳。
            if (millis() - irLastRequestMs < IR_DEBOUNCE_MS)
            {
                delay(5);
                continue;
            }

            portENTER_CRITICAL(&irMux);
            temp = pendingTemp;
            speed = pendingSpeed;
            mode = pendingMode;
            power = pendingPower;
            irSendPending = false;
            portEXIT_CRITICAL(&irMux);

            ledManager.stopBlink();
            ledManager.setColor(CRGB::Red);

            ac.next.degrees = temp;
            ac.next.fanspeed = (stdAc::fanspeed_t)speed;
            ac.next.mode = (stdAc::opmode_t)mode;
            ac.next.power = power;
            // 附加功能字段（turbo/light/sleep/clean/swing 等）保持 ac.next 中的状态，不重置

            DBG("准备发射红外信号...\n");
            DBG("当前空调协议: %s\n", lastProtocolName.c_str());
            DBG("待发射参数 - 温度: %d, 风速: %d, 模式: %d, 电源: %s\n", temp, speed, mode, power ? "开启" : "关闭");

            ac.sendAc();
            irTransmitCount++;
            delay(100);

            ledManager.off();

            if (isBLEMode)
            {
                if (bleManager.isConnected())
                {
                    ledManager.setColor(CRGB::Cyan); // BLE 已连接 = 青色常亮
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
        2,
        &irTaskHandle,
        1);
    Serial.println("红外发射任务已启动");
}

void IrManager::loop() {}

void IrManager::send(int temp, int speed, int mode, bool power)
{
    portENTER_CRITICAL(&irMux);
    if (irSendPending)
        irOverwriteCount++;
    pendingTemp = temp;
    pendingSpeed = speed;
    pendingMode = mode;
    pendingPower = power;
    irLastRequestMs = millis();
    irSendPending = true;
    irRequestCount++;
    portEXIT_CRITICAL(&irMux);
    DBG("已请求红外发射: 温度:%d℃, 风速:%d, 模式:%d, 电源:%s\n", temp, speed, mode, power ? "开启" : "关闭");
}

String IrManager::learnProtocol() { return ""; }

uint32_t IrManager::requestCount() const { return irRequestCount; }
uint32_t IrManager::transmitCount() const { return irTransmitCount; }
uint32_t IrManager::overwriteCount() const { return irOverwriteCount; }
uint32_t IrManager::taskStackFreeBytes() const
{
    if (irTaskHandle == nullptr)
        return 0;
    return (uint32_t)uxTaskGetStackHighWaterMark(irTaskHandle) * sizeof(StackType_t);
}
