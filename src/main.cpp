#include <Arduino.h>
#include "BleManager.h"
#ifndef BLE_ONLY
#include "WifiManagerEx.h"
#include "MqttManager.h"
#endif
#include "IrManager.h"
#include "SensorManager.h"
#include "LedManager.h"
#include "TimerManager.h"
#include "OtaManager.h"
#include "Debug.h"
#include "PinConfig.h"

// 其他必要库
#ifndef BLE_ONLY
#include <Ticker.h>
#endif
#include <SPIFFS.h>
#include "nvs_flash.h"
#ifndef BLE_ONLY
#include <homespan.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <map>
#include <DeviceConfig.h>
// ======================== 对象化模块 ========================
BleManager bleManager;
#ifndef BLE_ONLY
WifiManagerEx wifiManager;
MqttManager mqttManager;
#endif
IrManager irManager;
SensorManager sensorManager;
LedManager ledManager;
TimerManager timerManager;
OtaManager otaManager;

// 功能按键（引脚统一在 PinConfig.h 中管理）
#define KEY KEY_PIN

#define CMD_GET_PROTOCOL "get_protocol"
#define PROTOCOL_PREFIX "protocol="
#define PROTOCOL_INVALID "protocol=invalid"

bool deviceConnected = false;
bool oldDeviceConnected = false;

// 模式标志
bool isBLEMode = false;
bool isWiFiMode = false;

// 按键状态变量
bool buttonPressed = false;
unsigned long pressStartTime = 0;
bool isSwitching = false;
bool requestSwitchToWiFi = false;
bool requestSwitchToBLE = false;
bool requestFactoryReset = false;

// 环境数据缓存
float envTemperature = 25.0;
float acTargetTemp = 26.0;
float enHumidity = 50.0;

String lastProtocolName = "KELVINATOR"; // 默认协议
const uint16_t kIrLed = IR_TX_PIN;      // 红外发射
const uint16_t kRecvPin = IR_RX_PIN;    // 红外接收
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 50;
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = kTolerance;
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;
IRac ac(kIrLed);

// 红外接收器状态：防止重复 enableIRIn() 导致 ESP32 定时器/ISR 重复注册失败
bool irReceiverEnabled = false;
// 学习期间为 true：暂停主循环的 IRrecvDump，避免把学习要用的红外数据抢走
volatile bool irLearning = false;

void irEnableRecv()
{
  if (irReceiverEnabled)
    return;
  irrecv.enableIRIn();
  irReceiverEnabled = true;
  DBG("[IR] 红外接收已开启 (GPIO %d)\n", kRecvPin);
}

void irDisableRecv()
{
  if (!irReceiverEnabled)
    return;
  irrecv.disableIRIn();
  irReceiverEnabled = false;
  DBG("[IR] 红外接收已关闭\n");
}

bool wifiConnected = false;
bool shouldSaveConfig = false;
#ifndef BLE_ONLY
bool webServerActive = false; // 默认不启动Web服务器
Ticker ticker;                // 用于灯光闪烁的定时器
Ticker rgbBlinkTicker;        // WS2812B闪烁定时器
WebServer server(8080);
#endif

// 协议映射表（保持不变）

std::map<String, decode_type_t> protocolMap = {
    {"UNKNOWN", UNKNOWN},
    {"UNUSED", UNUSED},
    {"RC5", RC5},
    {"RC6", RC6},
    {"NEC", NEC},
    {"SONY", SONY},
    {"PANASONIC", PANASONIC},
    {"JVC", JVC},
    {"SAMSUNG", SAMSUNG},
    {"WHYNTER", WHYNTER},
    {"AIWA_RC_T501", AIWA_RC_T501},
    {"LG", LG},
    {"SANYO", SANYO},
    {"MITSUBISHI", MITSUBISHI},
    {"DISH", DISH},
    {"SHARP", SHARP},
    {"COOLIX", COOLIX},
    {"DAIKIN", DAIKIN},
    {"DENON", DENON},
    {"KELVINATOR", KELVINATOR},
    {"SHERWOOD", SHERWOOD},
    {"MITSUBISHI_AC", MITSUBISHI_AC},
    {"RCMM", RCMM},
    {"SANYO_LC7461", SANYO_LC7461},
    {"RC5X", RC5X},
    {"GREE", GREE},
    {"PRONTO", PRONTO},
    {"NEC_LIKE", NEC_LIKE},
    {"ARGO", ARGO},
    {"TROTEC", TROTEC},
    {"NIKAI", NIKAI},
    {"RAW", RAW},
    {"GLOBALCACHE", GLOBALCACHE},
    {"TOSHIBA_AC", TOSHIBA_AC},
    {"FUJITSU_AC", FUJITSU_AC},
    {"MIDEA", MIDEA},
    {"MAGIQUEST", MAGIQUEST},
    {"LASERTAG", LASERTAG},
    {"CARRIER_AC", CARRIER_AC},
    {"HAIER_AC", HAIER_AC},
    {"MITSUBISHI2", MITSUBISHI2},
    {"HITACHI_AC", HITACHI_AC},
    {"HITACHI_AC1", HITACHI_AC1},
    {"HITACHI_AC2", HITACHI_AC2},
    {"GICABLE", GICABLE},
    {"HAIER_AC_YRW02", HAIER_AC_YRW02},
    {"WHIRLPOOL_AC", WHIRLPOOL_AC},
    {"SAMSUNG_AC", SAMSUNG_AC},
    {"LUTRON", LUTRON},
    {"ELECTRA_AC", ELECTRA_AC},
    {"PANASONIC_AC", PANASONIC_AC},
    {"PIONEER", PIONEER},
    {"LG2", LG2},
    {"MWM", MWM},
    {"DAIKIN2", DAIKIN2},
    {"VESTEL_AC", VESTEL_AC},
    {"TECO", TECO},
    {"SAMSUNG36", SAMSUNG36},
    {"TCL112AC", TCL112AC},
    {"LEGOPF", LEGOPF},
    {"MITSUBISHI_HEAVY_88", MITSUBISHI_HEAVY_88},
    {"MITSUBISHI_HEAVY_152", MITSUBISHI_HEAVY_152},
    {"DAIKIN216", DAIKIN216},
    {"SHARP_AC", SHARP_AC},
    {"GOODWEATHER", GOODWEATHER},
    {"INAX", INAX},
    {"DAIKIN160", DAIKIN160},
    {"NEOCLIMA", NEOCLIMA},
    {"DAIKIN176", DAIKIN176},
    {"DAIKIN128", DAIKIN128},
    {"AMCOR", AMCOR},
    {"DAIKIN152", DAIKIN152},
    {"MITSUBISHI136", MITSUBISHI136},
    {"MITSUBISHI112", MITSUBISHI112},
    {"HITACHI_AC424", HITACHI_AC424},
    {"SONY_38K", SONY_38K},
    {"EPSON", EPSON},
    {"SYMPHONY", SYMPHONY},
    {"HITACHI_AC3", HITACHI_AC3},
    {"DAIKIN64", DAIKIN64},
    {"AIRWELL", AIRWELL},
    {"DELONGHI_AC", DELONGHI_AC},
    {"DOSHISHA", DOSHISHA},
    {"MULTIBRACKETS", MULTIBRACKETS},
    {"CARRIER_AC40", CARRIER_AC40},
    {"CARRIER_AC64", CARRIER_AC64},
    {"HITACHI_AC344", HITACHI_AC344},
    {"CORONA_AC", CORONA_AC},
    {"MIDEA24", MIDEA24},
    {"ZEPEAL", ZEPEAL},
    {"SANYO_AC", SANYO_AC},
    {"VOLTAS", VOLTAS},
    {"METZ", METZ},
    {"TRANSCOLD", TRANSCOLD},
    {"TECHNIBEL_AC", TECHNIBEL_AC},
    {"MIRAGE", MIRAGE},
    {"ELITESCREENS", ELITESCREENS},
    {"PANASONIC_AC32", PANASONIC_AC32},
    {"MILESTAG2", MILESTAG2},
    {"ECOCLIM", ECOCLIM},
    {"XMP", XMP},
    {"TRUMA", TRUMA},
    {"HAIER_AC176", HAIER_AC176},
    {"TEKNOPOINT", TEKNOPOINT},
    {"KELON", KELON},
    {"TROTEC_3550", TROTEC_3550},
    {"SANYO_AC88", SANYO_AC88},
    {"BOSE", BOSE},
    {"ARRIS", ARRIS},
    {"RHOSS", RHOSS},
    {"AIRTON", AIRTON},
    {"COOLIX48", COOLIX48},
    {"HITACHI_AC264", HITACHI_AC264},
    {"KELON168", KELON168},
    {"HITACHI_AC296", HITACHI_AC296},
    {"DAIKIN200", DAIKIN200},
    {"HAIER_AC160", HAIER_AC160},
    {"CARRIER_AC128", CARRIER_AC128},
    {"TOTO", TOTO},
    {"CLIMABUTLER", CLIMABUTLER},
    {"TCL96AC", TCL96AC},
    {"BOSCH144", BOSCH144},
    {"SANYO_AC152", SANYO_AC152},
    {"DAIKIN312", DAIKIN312},
    {"GORENJE", GORENJE},
    {"WOWWEE", WOWWEE},
    {"CARRIER_AC84", CARRIER_AC84},
    {"YORK", YORK}};

// ======================== 原有函数声明 ========================
decode_type_t getProtocolEnumFromString(const String &proto);
bool updateProtocolFromString(const String &protocolName, decode_type_t &targetProtocol);
String handleIrReceiving();
void configModeCallback();
void tick();
void initWifiManager();
void checkWiFiConnection();
void Web_set();
void key_init();
void key_scan();
void IRrecvDump(void);
void AC_SET_DATA(int temp, int speed, int mode, bool power = true);

// 协议转换函数（保持不变）
decode_type_t getProtocolEnumFromString(const String &proto)
{
  if (protocolMap.count(proto))
  {
    return protocolMap[proto];
  }
  else
  {
    return decode_type_t::UNKNOWN;
  }
}

void saveProtocolToSPIFFS(const String &protocolName)
{
  File file = SPIFFS.open("/protocol.txt", "w");
  if (file)
  {
    file.println(protocolName);
    file.close();
    Serial.println("协议已保存到SPIFFS: " + protocolName);
  }
  else
  {
    Serial.println("保存协议失败");
  }
}

String loadProtocolFromSPIFFS()
{
  if (!SPIFFS.exists("/protocol.txt"))
  {
    Serial.println("未找到保存的协议，使用默认协议");
    return "KELVINATOR";
  }

  File file = SPIFFS.open("/protocol.txt", "r");
  if (file)
  {
    String protocol = file.readStringUntil('\n');
    protocol.trim();
    file.close();
    Serial.println("从SPIFFS读取协议: " + protocol);
    return protocol;
  }
  Serial.println("读取协议失败，使用默认协议");
  return "KELVINATOR";
}

bool updateProtocolFromString(const String &protocolName, decode_type_t &targetProtocol)
{
  decode_type_t protoEnum = getProtocolEnumFromString(protocolName);
  if (protoEnum != decode_type_t::UNKNOWN)
  {
    targetProtocol = protoEnum;
    lastProtocolName = protocolName;
    saveProtocolToSPIFFS(protocolName);
    Serial.println("更新协议为：" + protocolName);
    return true;
  }
  else
  {
    Serial.println("无法识别协议：" + protocolName);
    return false;
  }
}

// 红外接收处理（保持不变）
String handleIrReceiving()
{
  if (irrecv.decode(&results))
  {
    String protoName = typeToString(results.decode_type);
    Serial.println("识别到的协议：" + protoName);

    String description = IRAcUtils::resultAcToString(&results);
    if (description.length())
    {
      Serial.println("空调信息：" + description);
    }

    irrecv.resume();
    return protoName;
  }
  return "";
}

// 恢复出厂设置：清除 HomeKit 配对、WiFi 凭据、SPIFFS（协议/定时任务），然后重启
void factoryReset()
{
  Serial.println("*** 恢复出厂设置：清除 HomeKit 配对 / WiFi / 定时任务 ***");
  ledManager.blinkWhite(); // 恢复出厂 = 白色闪烁
  delay(100);
  // 只删除已知配置文件（比整盘 SPIFFS.format() 快很多，避免恢复出厂等好几秒）
  if (SPIFFS.remove("/protocol.txt"))
    Serial.println("已删除协议配置文件");
  if (SPIFFS.remove("/timers.txt"))
    Serial.println("已删除定时任务文件");
  if (SPIFFS.remove("/wifi.json"))
    Serial.println("已删除WiFi配置文件");
  nvs_flash_erase(); // 清掉 HomeSpan 的配对、WiFi 等全部 NVS 数据
  delay(100);
  ESP.restart();
}

#ifndef BLE_ONLY
// WiFi关闭函数（保持不变）
void disableWiFi()
{
  Serial.println("关闭WiFi...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  server.stop();
  webServerActive = false;
  Serial.println("WiFi已关闭");
}

#endif

#ifndef BLE_ONLY
void handleBootButton()
{
  static bool lastKeyState = HIGH;
  static bool keySeenHigh = false; // 开机后是否至少观察到一次“松开”（防止引脚被拉死导致误触发）
  static unsigned long lastDebounceTime = 0;
  static bool longPressPending = false; // 超过短按时长、还没到重置时长的长按状态
  const unsigned long debounceDelay = 50;

  int currentKeyState = digitalRead(KEY);
  unsigned long currentTime = millis();

  if (currentKeyState == HIGH)
    keySeenHigh = true;

  if (currentKeyState != lastKeyState)
  {
    lastDebounceTime = currentTime;
    lastKeyState = currentKeyState;
    return;
  }

  if ((currentTime - lastDebounceTime) > debounceDelay)
  {
    if (currentKeyState == LOW)
    {
      if (!keySeenHigh)
        return; // 引脚从开机就一直是低电平：视为硬件异常，忽略假按键
      if (!buttonPressed && !isSwitching)
      {
        buttonPressed = true;
        pressStartTime = currentTime;
        longPressPending = false;
        Serial.println("按键已按下");
      }
      else if (buttonPressed && !isSwitching)
      {
        // 按住不放：1.5s 后黄灯提示“即将重置”，3s 触发恢复出厂
        unsigned long holdTime = currentTime - pressStartTime;
        if (holdTime >= 1500 && !longPressPending)
        {
          longPressPending = true;
          ledManager.setColor(CRGB::Yellow);
          Serial.println("长按中：继续按住将恢复出厂设置");
        }
        if (holdTime >= 3000)
        {
          Serial.println("长按触发：恢复出厂设置");
          factoryReset();
        }
      }
    }
    else
    {
      if (buttonPressed)
      {
        buttonPressed = false;
        unsigned long pressDuration = currentTime - pressStartTime;
        Serial.printf("按键释放，按压时长: %lu ms\n", pressDuration);

        if (longPressPending)
        {
          // 在 1.5~3s 之间松手：取消重置，恢复模式指示灯
          longPressPending = false;
          if (isWiFiMode)
          {
            if (wifiManager.isConnected())
              ledManager.off(); // WiFi 已连接 = 熄灭
            else
              ledManager.blinkGreen(); // WiFi 未连接 = 绿灯闪烁
          }
          else if (bleManager.isConnected())
            ledManager.setColor(CRGB::Cyan); // BLE 已连接 = 青色常亮
          else
            ledManager.blinkBlue();
          Serial.println("长按已取消");
          return;
        }

        if (pressDuration >= 100 && pressDuration < 1500 && !isSwitching)
        {
          isSwitching = true;
          if (isBLEMode)
          {
            requestSwitchToWiFi = true;
            Serial.println("短按触发：BLE -> WiFi");
          }
          else if (isWiFiMode)
          {
            requestSwitchToBLE = true;
            Serial.println("短按触发：WiFi -> BLE");
          }
          else
          {
            requestSwitchToBLE = true;
            Serial.println("短按触发：默认 -> BLE");
          }
        }
      }
    }
  }
}
#else
void handleBootButton()
{
  // 纯BLE版本：按键暂不用于模式切换
}
#endif

#ifndef BLE_ONLY
// WiFi配置回调（保持不变）
void configModeCallback()
{
  Serial.println("进入配置模式");
  Serial.println(WiFi.softAPIP());
  // 配置模式：绿灯闪烁
  ledManager.blinkGreen();
}

#endif

// 废弃的tick函数
void tick() {}

#ifndef BLE_ONLY
// WiFi初始化（保持不变）
void initWifiManager()
{
  // 使用WifiManagerEx对象
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi连接失败，开启配置门户...");
    wifiManager.enable();
  }
}

// WiFi连接检查（更新灯光状态）
void checkWiFiConnection()
{
  static unsigned long lastAttemptTime = 0;
  const unsigned long retryInterval = 5000;

  if (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - lastAttemptTime >= retryInterval)
    {
      Serial.println("WiFi已断开，尝试重连...");
      WiFi.reconnect();
      lastAttemptTime = millis();

      if (WiFi.status() != WL_CONNECTED)
      {
        Serial.println("重连失败，进入配置模式...");
        // WiFi未连接：绿灯闪烁
        ledManager.blinkGreen();
        initWifiManager();
      }
      else
      {
        // 重连成功：关闭绿灯
        ledManager.off();
      }
    }
  }
  else
  {
    // WiFi已连接：关闭绿灯
    ledManager.off();
  }
}
#endif

#ifndef BLE_ONLY
struct DEV_AC : Service::Thermostat
{
  int acPin;
  SpanCharacteristic *currentTemp;
  SpanCharacteristic *targetTemp;
  SpanCharacteristic *currentHumidity;
  SpanCharacteristic *thermostatMode;
  SpanCharacteristic *currentState;
  SpanCharacteristic *fanSpeed;
  SpanCharacteristic *fanDirection;

  DEV_AC(int pin) : Service::Thermostat()
  {
    acPin = pin;

    currentTemp = new Characteristic::CurrentTemperature(envTemperature);
    currentTemp->setRange(0, 50, 0.1);

    targetTemp = new Characteristic::TargetTemperature(26);
    targetTemp->setRange(17.0, 30.0, 1.0);

    currentHumidity = new Characteristic::CurrentRelativeHumidity(50);
    currentHumidity->setRange(0, 100, 0.1);

    thermostatMode = new Characteristic::TargetHeatingCoolingState(0);
    currentState = new Characteristic::CurrentHeatingCoolingState(0);
    new Characteristic::TemperatureDisplayUnits(0); // 0=摄氏度，必须属于恒温器服务

    Service::Fan *fan = new Service::Fan();
    new Characteristic::Active();
    fanSpeed = new Characteristic::RotationSpeed(0);
    fanSpeed->setRange(0, 100, 25); // 5 档：0/25/50/75/100
    fanDirection = new Characteristic::RotationDirection(0);

    pinMode(acPin, OUTPUT);
  }

  boolean update()
  {
    acTargetTemp = targetTemp->getNewVal();
    int hkMode = thermostatMode->getNewVal();
    int fanSpeedPercent = fanSpeed->getNewVal();

    int acSpeed;
    if (fanSpeedPercent == 0)
    {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kAuto);
    }
    else if (fanSpeedPercent <= 25)
    {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kLow);
    }
    else if (fanSpeedPercent <= 50)
    {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kMedium);
    }
    else if (fanSpeedPercent <= 75)
    {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kHigh);
    }
    else
    {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kMax);
    }

    Serial.println("\n====== HomeKit 控制指令 ======");
    Serial.printf("当前环境: %.1f℃ | 湿度: %.1f%%\n", envTemperature, enHumidity);

    const char *hkModeNames[] = {"关闭", "制热", "制冷", "自动"};
    const char *speedNames[] = {"自动", "固定", "低速", "中速", "高速"};

    int hkMode_get = 0;

    if (hkMode == 0)
    {
      hkMode_get = static_cast<int>(stdAc::opmode_t::kOff);
    }
    else if (hkMode == 1)
    {
      hkMode_get = static_cast<int>(stdAc::opmode_t::kHeat);
    }
    else if (hkMode == 2)
    {
      hkMode_get = static_cast<int>(stdAc::opmode_t::kCool);
    }
    else if (hkMode == 3)
    {
      hkMode_get = static_cast<int>(stdAc::opmode_t::kAuto);
    }

    // HomeKit “关闭”时真正发送 power=false，避免“开机+模式=关闭”的无效组合
    bool powerOn = (hkMode != 0);
    int sendMode = hkMode_get;
    if (!powerOn)
    {
      sendMode = static_cast<int>(ac.next.mode);
    }
    int sendSpeed = (hkMode == 3) ? 0 : acSpeed;
    irManager.send(acTargetTemp, sendSpeed, sendMode, powerOn);
    Serial.println("============================");
    return true;
  }

  void loop()
  {
    if (currentTemp->timeVal() > 2000)
    {
      currentTemp->setVal(envTemperature);
      currentHumidity->setVal(enHumidity);
    }
  };
};

// HomeKit 开关：开 = WiFi 模式，关 = BLE 模式（与 App“切换到WiFi模式”对应）
struct DEV_MODE_SWITCH : Service::Switch
{
  SpanCharacteristic *modeOn;

  DEV_MODE_SWITCH() : Service::Switch()
  {
    modeOn = new Characteristic::On(false);
    modeOn->setDescription("BLE/WiFi 模式切换");
  }

  boolean update()
  {
    bool wantWifi = modeOn->getNewVal();
    if (wantWifi && isBLEMode)
    {
      requestSwitchToWiFi = true;
      Serial.println("HomeKit切换：BLE -> WiFi");
    }
    else if (!wantWifi && isWiFiMode)
    {
      requestSwitchToBLE = true;
      Serial.println("HomeKit切换：WiFi -> BLE");
    }
    return true;
  }

  void loop()
  {
    if (modeOn->timeVal() > 2000)
    {
      modeOn->setVal(isWiFiMode);
    }
  }
};

#endif

#ifndef BLE_ONLY
// Web服务器设置（修改红外学习灯光）
void Web_set()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS 初始化失败");
    return;
  }

  server.on("/", HTTP_GET, []()
            {
    File file = SPIFFS.open("/index.html", "r");
    if(!file){
      server.send(404, "text/plain", "文件未找到");
      return;
    }

    String html = file.readString();
    file.close();
    server.send(200, "text/html", html); });

  server.on("/set", HTTP_GET, []()
            {
    String temp = server.arg("temp");
    String mode = server.arg("mode");
    String speed = server.arg("speed");
    String protocol = server.arg("protocol");

    Serial.printf("[网页设置] 收到参数 - 温度: %s°C, 模式: %s, 风速: %s, 协议: %s\n",
                 temp.c_str(), mode.c_str(), speed.c_str(), protocol.c_str());

    int temperature = temp.toInt();
    int modeValue = mode.toInt();
    int speedValue = speed.toInt();

    if (!protocol.isEmpty()) {
      updateProtocolFromString(protocol, ac.next.protocol);
    }

    AC_SET_DATA(temperature, speedValue, modeValue);

    String response = "温度=" + temp + "°C, 模式=" + mode + ", 风速=" + speed;
    if (!protocol.isEmpty()) {
      response += ", 协议=" + protocol;
    }
    server.send(200, "text/plain", response); });

  server.on("/protocol", HTTP_GET, []()
            { server.send(200, "text/plain", lastProtocolName); });

  // Web端红外学习（紫灯亮）
  server.on("/learn", HTTP_GET, []()
            {
    Serial.println("开始协议学习...");
    ledManager.blinkPurple(); // 学习模式：紫灯亮
    irLearning = true; // 暂停主循环红外解析，数据只给学习流程
    bool wasRecvEnabled = irReceiverEnabled; // 记录学习前状态，学习后恢复
    irEnableRecv(); // 学习期间开启红外接收（已开启则跳过，避免重复初始化）

    unsigned long startTime = millis();
    String detectedProtocol_web = "";

    while (detectedProtocol_web.isEmpty() && (millis() - startTime < 10000)) {
      String p = handleIrReceiving();
      // UNKNOWN 是噪声/半截帧，不算识别成功，继续等待真正的协议
      if (!p.isEmpty() && p != "UNKNOWN") detectedProtocol_web = p;
      delay(100);
    }

    irLearning = false;
    if (!wasRecvEnabled) irDisableRecv(); // 学习前未开启则恢复关闭
    ledManager.off(); // 学习结束：紫灯关闭

    if (!detectedProtocol_web.isEmpty()) {
      updateProtocolFromString(detectedProtocol_web, ac.next.protocol);
      server.send(200, "text/plain", "协议学习成功: " + detectedProtocol_web);
    } else {
      server.send(200, "text/plain", "学习超时，未接收到有效信号");
    } });

  server.on("/toggle", HTTP_GET, []()
            {
    webServerActive = !webServerActive;
    String status = webServerActive ? "WebServer is active now." : "WebServer is disabled now.";
    server.send(200, "text/plain", status); });

  server.on("/sensor", HTTP_GET, []()
            {
    if (isnan(envTemperature) || isnan(enHumidity)) {
      server.send(500, "application/json", "{\"error\":\"传感器读取失败\"}");
      return;
    }

    String json = "{\"temp\":" + String(envTemperature,1) +
                 ",\"humidity\":" + String(enHumidity,1) + "}";
    server.send(200, "application/json", json); });

  server.on("/power", HTTP_GET, []()
            {
    static bool powerState = false;
    powerState = !powerState;

    if (powerState) {
      AC_SET_DATA(26, 3, 1);
      server.send(200, "text/plain", "空调已开启");
    } else {
      ac.next.power = false;
      ac.sendAc();
      server.send(200, "text/plain", "空调已关闭");
    } });
}
#endif

// 按键初始化（保持不变）

void key_init()
{
  ledManager.begin();
  pinMode(KEY, INPUT_PULLUP);
}

// 按键扫描（保持不变）
void key_scan()
{
#ifndef BLE_ONLY
  handleBootButton();
#endif
}

// 红外接收打印（保持不变）
void IRrecvDump(void)
{
  if (irrecv.decode(&results))
  {
    String protoName = typeToString(results.decode_type);
    Serial.println("Protocol:" + protoName);
    Serial.print(resultToHumanReadableBasic(&results));

    String description = IRAcUtils::resultAcToString(&results);
    if (description.length())
      Serial.println(D_STR_MESGDESC ": " + description);
    yield();
#if LEGACY_TIMING_INFO
    Serial.println(resultToTimingInfo(&results));
    yield();
#endif
    Serial.println();
    yield();
    irrecv.resume();
  }
}

// 红外发射参数设置（保持不变）
void AC_SET_DATA(int temp, int speed, int mode, bool power)
{
  irManager.send(temp, speed, mode, power);
  Serial.printf("已请求红外发射: 温度:%d℃, 风速:%d, 模式:%d, 电源:%s\n", temp, speed, mode, power ? "开启" : "关闭");
}

// 系统初始化
// 修改 setup 函数中的初始化部分

void setup()
{
  Serial.begin(115200);
  Serial2.begin(115200);
  while (!Serial)
    delay(50);
  bleManager.begin();
  key_init();

  // 初始化各大模块
  // bleManager.begin();
#ifndef BLE_ONLY
  wifiManager.begin();
  mqttManager.begin();
  // BLE 指令的响应同时转发给 MQTT（远程控制也能拿到结果）
  bleManager.responseForwarder = [](const String &s)
  {
    mqttManager.publish(s);
  };
#endif
  irManager.begin();
  sensorManager.begin();
  Serial.println("版本号：" + String(FW_VERSION));
  // 详细日志开启时同时开启红外接收解析（串口打印收到的红外信号）
#ifdef DEBUG_LOG
  irEnableRecv();
#else
  // 平时不开启红外接收器（默认关闭，省 CPU），协议学习时再 enableIRIn/disableIRIn
#endif

  // 其他初始化
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS初始化失败");
  }
  otaManager.begin();

  // 定时任务依赖 SPIFFS 存储，必须在 SPIFFS 挂载之后再初始化
  timerManager.begin();

  // 从SPIFFS读取保存的协议
  lastProtocolName = loadProtocolFromSPIFFS();

  // 初始化空调状态
  updateProtocolFromString(lastProtocolName, ac.next.protocol);
  ac.next.model = 1;
  ac.next.celsius = true;
  ac.next.degrees = 25;
  ac.next.fanspeed = stdAc::fanspeed_t::kMedium;
  ac.next.swingv = stdAc::swingv_t::kOff;
  ac.next.swingh = stdAc::swingh_t::kOff;
  ac.next.light = true;
  ac.next.beep = false;
  ac.next.econo = false;
  ac.next.filter = false;
  ac.next.turbo = false;
  ac.next.quiet = false;
  ac.next.sleep = -1;
  ac.next.clean = false;
  ac.next.clock = -1;
  ac.next.power = true;

  // HomeSpan 设置（仅保留 HomeKit/WiFi 功能的版本）
#ifndef BLE_ONLY
  homeSpan.setPairingCode("11122333");
  homeSpan.begin(Category::AirConditioners, "空调");
  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new DEV_AC(15);
  new DEV_MODE_SWITCH();
#endif

  // 默认启动模式：完整版=BLE；WiFi专用版=WiFi
#ifndef WIFI_ONLY
  isBLEMode = true;
  isWiFiMode = false;
  bleManager.enable();
  Serial.println("系统初始化完成,默认启动BLE模式");
#else
  isBLEMode = false;
  isWiFiMode = true;
  wifiManager.enable();
  Serial.println("系统初始化完成,默认启动WiFi模式");
#endif
}

void sendEnvironmentDataIfNeeded()
{
  if (!bleManager.isConnected())
    return;
  bleManager.sendTempHumidity(envTemperature, enHumidity);
}

void loop()
{
  delay(50);

  if (requestFactoryReset)
  {
    requestFactoryReset = false;
    factoryReset();
  }
  if (irReceiverEnabled && !irLearning)
    IRrecvDump(); // 接收器开启且不在学习时才解析打印
#ifndef BLE_ONLY
  handleBootButton();

#ifndef WIFI_ONLY
  if (requestSwitchToWiFi)
  {
    requestSwitchToWiFi = false;
    Serial.println("正在切换到WiFi模式...");
    bleManager.disable();
    isBLEMode = false;
    isWiFiMode = true;
    ledManager.blinkGreen(); // WiFi 等待连接 = 绿色闪烁
    delay(300);
    wifiManager.enable();
    timerManager.syncTime();
    isSwitching = false;
  }
  if (requestSwitchToBLE)
  {
    requestSwitchToBLE = false;
    Serial.println("正在切换到BLE模式...");
    wifiManager.disable();
    mqttManager.disconnect();
    isBLEMode = true;
    isWiFiMode = false;
    ledManager.blinkBlue();
    bleManager.enable();
    isSwitching = false;
  }
#endif
#endif

  if (isBLEMode)
  {
    bleManager.loop();
    sendEnvironmentDataIfNeeded();
    timerManager.loop(); // 定时任务检查（BLE 模式也要执行）
  }
  if (isWiFiMode)
  {
#ifndef BLE_ONLY
    wifiManager.loop();
    mqttManager.loop();
    homeSpan.poll();
    timerManager.loop();
#else
    timerManager.loop();
#endif
  }
#ifdef DEBUG_LOG
  // IRrecvDump(); // 详细日志模式下解析并打印收到的红外信号
#endif
  irManager.loop();
  sensorManager.loop();
  ledManager.update();
}
