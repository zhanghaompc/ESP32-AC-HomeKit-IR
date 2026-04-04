//rgb 稳定版


#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <IRac.h>
#include <IRutils.h>
#include <assert.h>
#include <IRrecv.h>
#include <IRtext.h>
#include <map>
#include <Ticker.h>    
#include <SPIFFS.h>
#include <Adafruit_AHTX0.h>
#include <homespan.h>
// BLE相关库
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
// WS2812B驱动库
#include <FastLED.h>

// ======================== WS2812B配置 ========================
#define NUM_LEDS 1           // 只有1个WS2812B灯珠
#define DATA_PIN 2           // WS2812B数据引脚（可根据实际修改）
CRGB leds[NUM_LEDS];         // 创建LED数组

// 颜色定义（RGB格式）
#define COLOR_OFF CRGB::Black
#define COLOR_RED CRGB::Red
#define COLOR_GREEN CRGB::Green
#define COLOR_BLUE CRGB::Blue
#define COLOR_PURPLE CRGB::Purple  // 紫色

// BLE配置
#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// 模式状态变量
bool isWiFiEnabled = true;    // 默认WiFi模式
bool isBLEEnabled = false;
unsigned long pressStartTime = 0;
bool buttonPressed = false;
// 按键状态变量
bool requestSwitchToWiFi = false;
bool requestSwitchToBLE = false;
// BLE相关对象
BLEServer* pBLEServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
// 红外任务句柄
TaskHandle_t irTaskHandle = NULL;
// 互斥锁（保护共享资源）
portMUX_TYPE irMux = portMUX_INITIALIZER_UNLOCKED;
// 红外任务控制标志
bool irTaskRunning = false;
// 红外发射请求变量
volatile bool irSendPending = false;
volatile int pendingTemp = 25;
volatile int pendingSpeed = 2; // 默认中等风速
volatile int pendingMode = 1;  // 默认制冷模式
// 红外发射请求结构体
struct IrRequest {
  int temp;
  int speed;
  int mode;
  bool valid; // 标记请求是否有效
};

// 数据更新相关变量
unsigned long lastBleDataSend = 0;  
const unsigned long BLE_DATA_SEND_INTERVAL = 15000; // 15秒发送间隔
const float TEMPERATURE_CHANGE_THRESHOLD = 0.5;    
const float HUMIDITY_CHANGE_THRESHOLD = 1.0;       
float lastSentTemperature = 0.0;
float lastSentHumidity = 0.0;

// 硬件引脚定义
#define KEY 0       // BOOT键，用于WiFi/BLE切换
#define KEY1 4
#define KEY2 14

Adafruit_AHTX0 aht;
bool sensorOnline = false;        
unsigned long lastSensorRead = 0; 
float envTemperature = 25.0;  
float acTargetTemp = 26.0;    
float enHumidity = 50.0;     

String lastProtocolName = "KELVINATOR";  
const uint16_t kIrLed = 16;  
const uint16_t kRecvPin = 23;  
const uint16_t kCaptureBufferSize = 1024;  
const uint8_t kTimeout = 50;
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = kTolerance;  
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;  
IRac ac(kIrLed);     

bool wifiConnected = false;
bool shouldSaveConfig = false;
bool webServerActive = true;  
Ticker ticker;          // 用于灯光闪烁的定时器
Ticker rgbBlinkTicker;  // WS2812B闪烁定时器
WebServer server(8080);

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
  {"YORK", YORK}
};
// ======================== WS2812B控制函数 ========================
// 灯光闪烁状态（0:关闭 1:绿灯闪 2:蓝灯闪）
int rgbBlinkState = 0;
// 闪烁状态变量
bool greenBlinkState = false;
bool blueBlinkState = false;

// 初始化WS2812B
void initWS2812B() {
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(100); // 设置亮度（0-255），避免过亮
  leds[0] = COLOR_OFF;
  FastLED.show();
}

// 设置WS2812B颜色
void setWS2812BColor(CRGB color) {
  leds[0] = color;
  FastLED.show();
}

// 绿灯闪烁回调函数（WiFi未连接时）
void blinkGreen() {
  greenBlinkState = !greenBlinkState;
  if (greenBlinkState) setWS2812BColor(COLOR_GREEN);
  else setWS2812BColor(COLOR_OFF);
}

// 蓝灯闪烁回调函数（BLE未连接时）
void blinkBlue() {
  blueBlinkState = !blueBlinkState;
  if (blueBlinkState) setWS2812BColor(COLOR_BLUE);
  else setWS2812BColor(COLOR_OFF);
}

// 停止所有灯光闪烁
void stopAllBlinks() {
  rgbBlinkTicker.detach();
  rgbBlinkState = 0;
  setWS2812BColor(COLOR_OFF);
}

// ======================== 原有函数声明 ========================
decode_type_t getProtocolEnumFromString(const String& proto);
bool updateProtocolFromString(const String& protocolName, decode_type_t& targetProtocol);
String handleIrReceiving();
void configModeCallback(WiFiManager *myWiFiManager);
void tick();
void initWifiManager();
void checkWiFiConnection();
void readSensorData();
void Web_set();
void key_init();
void key_scan();
void handleCommand(String command);
void IRrecvDump(void);
void AC_SET_DATA(int temp, int speed, int mode);
void enableWiFi();
void disableBLE();

// 协议转换函数（保持不变）
decode_type_t getProtocolEnumFromString(const String& proto) {
  if (protocolMap.count(proto)) {
    return protocolMap[proto];
  } else {
    return decode_type_t::UNKNOWN;
  }
}

bool updateProtocolFromString(const String& protocolName, decode_type_t& targetProtocol) {
  portENTER_CRITICAL(&irMux);
  decode_type_t protoEnum = getProtocolEnumFromString(protocolName);
  if (protoEnum != decode_type_t::UNKNOWN) {
    targetProtocol = protoEnum;
    lastProtocolName = protocolName;
    Serial.println("更新协议为：" + protocolName);
    portEXIT_CRITICAL(&irMux);
    return true;
  } else {
    Serial.println("无法识别协议：" + protocolName);
    portEXIT_CRITICAL(&irMux);
    return false;
  }
}

// BLE服务器回调（更新连接状态与灯光）
class MyBLEServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("BLE客户端已连接");
    // BLE连接成功：蓝灯常亮
    stopAllBlinks();
    setWS2812BColor(COLOR_BLUE);
  };

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("BLE客户端已断开");
    requestSwitchToWiFi = true;
    Serial.println("蓝牙断开，切换回WiFi模式");
  }
};

// 红外接收处理（保持不变）
String handleIrReceiving() {
  if (irrecv.decode(&results)) {
    String protoName = typeToString(results.decode_type);
    Serial.println("识别到的协议：" + protoName);
    
    String description = IRAcUtils::resultAcToString(&results);
    if (description.length()) {
      Serial.println("空调信息：" + description);
    }
    
    irrecv.resume();
    return protoName;
  }
  return "";
}

// BLE特征回调（处理命令）
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.empty()) return;

    String command = String(rxValue.c_str());
    Serial.println("收到小程序命令: " + command);

    // 1. 电源开关命令
    if (command.startsWith("power=")) {
      String powerState = command.substring(6);
      ac.next.power = (powerState == "on");
      ac.sendAc();
      Serial.print("空调电源已");
      Serial.println(ac.next.power ? "开启" : "关闭");
      pTxCharacteristic->setValue(("power=" + powerState).c_str());
      pTxCharacteristic->notify();
      return;
    }

    // 2. 复合控制命令
    if (command.startsWith("temp=")) {
      int temp = command.substring(5, command.indexOf(';')).toInt();
      command = command.substring(command.indexOf(';') + 1);
      
      int mode = command.substring(5, command.indexOf(';')).toInt();
      command = command.substring(command.indexOf(';') + 1);
      
      int speed = command.substring(6, command.indexOf(';')).toInt();
      command = command.substring(command.indexOf(';') + 1);
      
      String powerState = command.substring(6);
      
      AC_SET_DATA(temp, speed, mode);
      pTxCharacteristic->setValue(("已设置:温度=" + String(temp) + "℃ 模式=" + String(mode)).c_str());
      pTxCharacteristic->notify();
      return;
    }

    // 3. 协议设置命令
    if (command.startsWith("protocol=")) {
      String proto = command.substring(9);
      bool success = updateProtocolFromString(proto, ac.next.protocol);
      pTxCharacteristic->setValue(success ? ("protocol=" + proto).c_str() : "protocol=invalid");
      pTxCharacteristic->notify();
      return;
    }

    // 4. 学习模式命令（红外学习时亮紫灯）
    if (command == "learn=start") {
      Serial.println("进入协议学习模式...");
      pTxCharacteristic->setValue("learn=waiting");
      pTxCharacteristic->notify();
      
      setWS2812BColor(COLOR_PURPLE); // 学习模式：紫灯亮
      unsigned long startTime = millis();
      String detectedProtocol = "";
      
      while (detectedProtocol.isEmpty() && (millis() - startTime < 10000)) {
        detectedProtocol = handleIrReceiving();
        delay(100);
      }
      
      setWS2812BColor(COLOR_OFF); // 学习结束：紫灯关闭
        // 关键修复：恢复蓝灯常亮
      if (deviceConnected) {
        setWS2812BColor(COLOR_BLUE);
      } 
      if (!detectedProtocol.isEmpty()) {
        updateProtocolFromString(detectedProtocol, ac.next.protocol);
        pTxCharacteristic->setValue(("learn=success:" + detectedProtocol).c_str());
      } else {
        pTxCharacteristic->setValue("learn=timeout");
      }
      pTxCharacteristic->notify();
      return;
    }

    // 5. 状态查询命令
    if (command == "status") {
      String status = "temp=" + String(envTemperature, 1) + 
                     ";humidity=" + String(enHumidity, 1) + 
                     ";power=" + (ac.next.power ? "on" : "off");
      pTxCharacteristic->setValue(status.c_str());
      pTxCharacteristic->notify();
      return;
    }

    // 未知命令回复
    pTxCharacteristic->setValue(("unknown_cmd:" + command).c_str());
    pTxCharacteristic->notify();
  }
};

// WiFi模式启用（更新灯光状态）
void enableWiFi() {
  if (isWiFiEnabled) return;
  
  Serial.println("开启WiFi...");
  
  if (isBLEEnabled) {
    disableBLE();
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(); 
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(100);
    Serial.print(".");
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n自动连接失败，启动配置门户");
    // WiFi未连接：绿灯闪烁提醒
    rgbBlinkState = 1;
    rgbBlinkTicker.attach(0.5, blinkGreen); // 0.5秒闪烁一次
    initWifiManager();
  } else {
    // WiFi连接成功：关闭绿灯
    stopAllBlinks();
    setWS2812BColor(COLOR_OFF);
  }
  
  server.begin();
  webServerActive = true;
  
  Serial.print("WiFi已开启，IP: ");
  Serial.println(WiFi.localIP());
  
  isWiFiEnabled = true;
  isBLEEnabled = false;
}

// WiFi关闭函数（保持不变）
void disableWiFi() {
  Serial.println("关闭WiFi...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  server.stop();
  webServerActive = false;
  Serial.println("WiFi已关闭");
}

// BLE模式启用（更新灯光状态）
void enableBLE() {
  if (isBLEEnabled) return;
  
  Serial.println("切换到BLE模式...");
  
  if (isWiFiEnabled) {
    disableWiFi();
  }

  if (pBLEServer) {
    BLEDevice::deinit();
    pBLEServer = NULL;
    delay(100);
  }
  
  BLEDevice::init("ESP32-AC");
  pBLEServer = BLEDevice::createServer();
  pBLEServer->setCallbacks(new MyBLEServerCallbacks());
  
  BLEService *pService = pBLEServer->createService(BLE_SERVICE_UUID);
  
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  pBLEServer->startAdvertising();

  // BLE未连接：蓝灯闪烁提醒
  rgbBlinkState = 2;
  rgbBlinkTicker.attach(0.5, blinkBlue); // 0.5秒闪烁一次
  
  isBLEEnabled = true;
  isWiFiEnabled = false;
  Serial.println("BLE初始化完成，等待小程序连接...");
}

// BLE关闭函数（保持不变）
void disableBLE() {
  if (!isBLEEnabled) return;
  
  Serial.println("关闭BLE...");
  
  if (pBLEServer && pBLEServer->getAdvertising()) {
    pBLEServer->getAdvertising()->stop();
  }
  
  if (pBLEServer) {
    uint16_t connId = pBLEServer->getConnId();
    if (connId != 0) {
      pBLEServer->disconnect(connId);
      delay(200);
    }
  }
  
  BLEDevice::deinit();
  pBLEServer = NULL;
  pTxCharacteristic = NULL;
  
  deviceConnected = false;
  isBLEEnabled = false;
  stopAllBlinks(); // 关闭BLE相关灯光
  Serial.println("BLE已彻底关闭");
}

// 按键处理（长按2秒切换到BLE模式）
void handleBootButton() {
  int keyState = digitalRead(KEY);
  
  if (keyState == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      pressStartTime = millis();
    } else {
      // 长按2秒触发BLE切换
      if (millis() - pressStartTime >= 2000 && !requestSwitchToBLE) {
        requestSwitchToBLE = true;
        Serial.println("检测到长按，请求切换到BLE模式");
      }
    }
  } else {
    if (buttonPressed) {
      buttonPressed = false;
      unsigned long pressDuration = millis() - pressStartTime;
      
      if (pressDuration < 2000) {
        // 短按切换到WiFi
        requestSwitchToWiFi = true;
        Serial.println("短按，请求切换到WiFi模式");
      }
    }
  }
}

// WiFi配置回调（保持不变）
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("进入配置模式");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  // 配置模式：绿灯闪烁
  rgbBlinkState = 1;
  rgbBlinkTicker.attach(0.5, blinkGreen);
} 

// 废弃的tick函数
void tick() {}

// WiFi初始化（保持不变）
void initWifiManager() {
  WiFiManager wifiManager;
  wifiManager.setTitle("永远相信美好的事物即将发生");
  wifiManager.setTimeout(60);
  wifiManager.setAPCallback(configModeCallback);

  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiManager.autoConnect("WIFI配置")) {
      Serial.println("WiFi连接失败，开启配置门户...");
      wifiManager.startConfigPortal("WIFI配置");
    }
  }
}

// WiFi连接检查（更新灯光状态）
void checkWiFiConnection() {
  if (!isWiFiEnabled) return;
  
  static unsigned long lastAttemptTime = 0;
  const unsigned long retryInterval = 5000;
  
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttemptTime >= retryInterval) {
      Serial.println("WiFi已断开，尝试重连...");
      WiFi.reconnect();
      lastAttemptTime = millis();
      
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("重连失败，进入配置模式...");
        // WiFi未连接：绿灯闪烁
        rgbBlinkState = 1;
        rgbBlinkTicker.attach(0.5, blinkGreen);
        initWifiManager();
      } else {
        // 重连成功：关闭绿灯
        stopAllBlinks();
        setWS2812BColor(COLOR_OFF);
      }
    }
  } else {
    // WiFi已连接：关闭绿灯
    stopAllBlinks();
    setWS2812BColor(COLOR_OFF);
  }
}

// 传感器读取（保持不变）
void readSensorData() {
  if (sensorOnline && (millis() - lastSensorRead > 2500)) {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    
    if (!isnan(temp.temperature)) {
      envTemperature = temp.temperature;
      enHumidity = humidity.relative_humidity;
    }
    lastSensorRead = millis();
  }
}

// HomeKit设备定义（保持不变）
struct DEV_AC : Service::Thermostat {
  int acPin;
  SpanCharacteristic *currentTemp;
  SpanCharacteristic *targetTemp;
  SpanCharacteristic *currentHumidity;
  SpanCharacteristic *thermostatMode;
  SpanCharacteristic *currentState;
  SpanCharacteristic *fanSpeed;
  SpanCharacteristic *fanDirection;
  
  DEV_AC(int pin) : Service::Thermostat() {
    acPin = pin;
    
    currentTemp = new Characteristic::CurrentTemperature(envTemperature);
    currentTemp->setRange(0, 50, 0.1);
    
    targetTemp = new Characteristic::TargetTemperature(26);
    targetTemp->setRange(17.0, 30.0, 1.0);
    
    currentHumidity = new Characteristic::CurrentRelativeHumidity(50);
    currentHumidity->setRange(0, 100, 1);
    
    thermostatMode = new Characteristic::TargetHeatingCoolingState(0);
    currentState = new Characteristic::CurrentHeatingCoolingState(0);
    
    Service::Fan *fan = new Service::Fan();
    new Characteristic::Active();
    fanSpeed = new Characteristic::RotationSpeed(0);
    fanSpeed->setRange(0, 100, 20);
    fanDirection = new Characteristic::RotationDirection(0);
    
    pinMode(acPin, OUTPUT);
  }
  
  boolean update() {
    acTargetTemp = targetTemp->getNewVal();
    int hkMode = thermostatMode->getNewVal();
    int fanSpeedPercent = fanSpeed->getNewVal();
    
    int acSpeed;
    if(fanSpeedPercent == 0) {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kAuto);
    } else if(fanSpeedPercent <= 25) {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kMin);
    } else if(fanSpeedPercent <= 50) {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kLow);
    } else if(fanSpeedPercent <= 75) {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kMedium);
    } else {
      acSpeed = static_cast<int>(stdAc::fanspeed_t::kHigh);
    }
    
    Serial.println("\n====== HomeKit 控制指令 ======");
    Serial.printf("当前环境: %.1f℃ | 湿度: %.1f%%\n", envTemperature, enHumidity);
    
    const char* hkModeNames[] = {"关闭", "制热", "制冷", "自动"};
    const char* speedNames[] = {"自动", "固定", "低速", "中速", "高速"};
    
    int finalSpeed = (hkMode == 3) ? 0 : acSpeed;
    int hkMode_get;
    
    if(hkMode == 0) {
      hkMode_get = static_cast<int>(stdAc::opmode_t::kOff);
    } else if(hkMode == 1) {
      hkMode_get = static_cast<int>(stdAc::opmode_t::kHeat);
    } else if(hkMode == 2) {
      hkMode_get = static_cast<int>(stdAc::opmode_t::kCool);
    } else if(hkMode == 3) {
      hkMode_get = static_cast<int>(stdAc::opmode_t::kAuto);
    }

    AC_SET_DATA(acTargetTemp, acSpeed, hkMode_get);
    Serial.println("============================");
    return true;  
  }
  
  void loop() {
    if (currentTemp->timeVal() > 2000) {
      currentTemp->setVal(envTemperature);
      currentHumidity->setVal(enHumidity);
    }
  };
};

// Web服务器设置（修改红外学习灯光）
void Web_set() {
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS 初始化失败");
    return;
  }
  
  server.on("/", HTTP_GET, []() {
    File file = SPIFFS.open("/index.html", "r");
    if(!file){
      server.send(404, "text/plain", "文件未找到");
      return;
    }

    String html = file.readString();
    file.close();
    server.send(200, "text/html", html);
  });

  server.on("/set", HTTP_GET, []() {
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
    server.send(200, "text/plain", response);
  });

  server.on("/protocol", HTTP_GET, []() {
    server.send(200, "text/plain", lastProtocolName);
  });

  // Web端红外学习（紫灯亮）
  server.on("/learn", HTTP_GET, []() {
    Serial.println("开始协议学习...");
    setWS2812BColor(COLOR_PURPLE); // 学习模式：紫灯亮
    
    unsigned long startTime = millis();
    String detectedProtocol_web = "";
    
    while (detectedProtocol_web.isEmpty() && (millis() - startTime < 10000)) {
      detectedProtocol_web = handleIrReceiving();
      delay(100);
    }
    
    setWS2812BColor(COLOR_OFF); // 学习结束：紫灯关闭
    
    if (!detectedProtocol_web.isEmpty()) {
      updateProtocolFromString(detectedProtocol_web, ac.next.protocol);
      server.send(200, "text/plain", "协议学习成功: " + detectedProtocol_web);
    } else {
      server.send(200, "text/plain", "学习超时，未接收到有效信号");
    }
  });

  server.on("/toggle", HTTP_GET, []() {
    webServerActive = !webServerActive;
    String status = webServerActive ? "WebServer is active now." : "WebServer is disabled now.";
    server.send(200, "text/plain", status);
  });
  
  server.on("/sensor", HTTP_GET, []() {
    if (isnan(envTemperature) || isnan(enHumidity)) {
      server.send(500, "application/json", "{\"error\":\"传感器读取失败\"}");
      return;
    }
    
    String json = "{\"temp\":" + String(envTemperature,1) + 
                 ",\"humidity\":" + String(enHumidity,1) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/power", HTTP_GET, []() {
    static bool powerState = false;
    powerState = !powerState;

    if (powerState) {
      AC_SET_DATA(26, 3, 1);
      server.send(200, "text/plain", "空调已开启");
    } else {
      ac.next.power = false;
      ac.sendAc();
      server.send(200, "text/plain", "空调已关闭");
    }
  });
}

// 按键初始化（保持不变）
void key_init() {
  // 初始化WS2812B
  initWS2812B();
  pinMode(KEY, INPUT_PULLUP);
  pinMode(KEY1, INPUT_PULLUP);
  pinMode(KEY2, INPUT_PULLUP);
}

// 按键扫描（保持不变）
void key_scan() {
  handleBootButton();
} 

// 命令处理（保持不变）
void handleCommand(String command) {
  if (command == "off") {
    ac.next.power = false;
    Serial.println("关闭空调。");
    ac.sendAc();
  }
  else if (command == "on") {
    ac.next.power = true;
    Serial.println("打开空调。");
    ac.sendAc();
  } 
  else if (command == "2") {
    Serial.println("已停止播放");
  } 
  else if (command == "3") {
    Serial.println("上一首");
  } 
  else if (command == "4") {
    Serial.println("下一首");
  }
}  

// 红外接收打印（保持不变）
void IRrecvDump(void) {
  if (irrecv.decode(&results)) {
    String protoName = typeToString(results.decode_type);
    Serial.println("Protocol:"+ protoName);
    Serial.print(resultToHumanReadableBasic(&results));
    
    String description = IRAcUtils::resultAcToString(&results);
    if (description.length()) Serial.println(D_STR_MESGDESC ": " + description);
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

// 红外任务函数（红外发射时亮红灯）
void irTaskFunction(void* parameter) {
  irTaskRunning = true;
  Serial.println("红外发射任务启动，运行在核心: " + String(xPortGetCoreID()));

  while (irTaskRunning) {
    if (irSendPending) {
      // 红外发射前：红灯亮
      setWS2812BColor(COLOR_RED);
      
      ac.next.degrees = pendingTemp;
      ac.next.fanspeed = (stdAc::fanspeed_t)pendingSpeed;
      ac.next.mode = (stdAc::opmode_t)pendingMode;
      ac.next.light = true;
      Serial.println("准备发射红外信号...");
      Serial.println("当前空调协议: " + lastProtocolName);
      Serial.println("待发射参数 - 温度: " + String(pendingTemp) + 
                     ", 风速: " + String(pendingSpeed) + 
                     ", 模式: " + String(pendingMode));

      // 执行发射
      ac.sendAc();
      delay(100); // 确保发射完成
      
      // 红外发射后：红灯关闭
      setWS2812BColor(COLOR_OFF);
      
      // 恢复蓝牙连接时的蓝灯常亮
      if (isBLEEnabled && deviceConnected) {
        setWS2812BColor(COLOR_BLUE);
      }
      
      Serial.println("当前空调的协议: " + lastProtocolName);
      Serial.printf("设置温度: %d, 风速: %d, 模式: %d（运行核心: %d）\n", 
                   pendingTemp, pendingSpeed, pendingMode, xPortGetCoreID());
      
      // 清除请求标志
      irSendPending = false;
    }

    delay(10);
  }
  
  vTaskDelete(NULL);
}

// 红外发射参数设置（保持不变）
void AC_SET_DATA(int temp, int speed, int mode) {
  pendingTemp = temp;
  pendingSpeed = speed;
  pendingMode = mode;
  irSendPending = true;
  Serial.printf("已请求红外发射: 温度:%d℃, 风速:%d, 模式:%d\n", temp, speed, mode);
}

// 系统初始化
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);
  while (!Serial) delay(50);
  
  assert(irutils::lowLevelSanityCheck() == 0);

  Serial.printf("\n" D_STR_IRRECVDUMP_STARTUP "\n", kRecvPin);
#if DECODE_HASH
  irrecv.setUnknownThreshold(kMinUnknownSize);
#endif
  irrecv.setTolerance(kTolerancePercentage);
  irrecv.enableIRIn();
  delay(200);
  
  key_init(); 
  initWifiManager();  
  Serial.println("connected...so easy :)");
  ticker.detach();
  Serial.println("WiFi Connected: " + WiFi.localIP().toString());
  
  // HomeSpan 设置
  homeSpan.setWifiCredentials(WiFi.SSID().c_str(), WiFi.psk().c_str());
  homeSpan.setApTimeout(300);
  homeSpan.enableAutoStartAP();
  homeSpan.setPairingCode("11122333");
  homeSpan.begin(Category::AirConditioners, "空调");
  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new DEV_AC(15);
  
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS初始化失败");
    return;
  }
  
  // 初始化传感器
  if (aht.begin()) {
    Serial.println("AHT20 初始化成功");
    sensorOnline = true;
  } else {
    Serial.println("无法找到 AHT20 传感器！");
    sensorOnline = false;
  }
  
  Web_set();
  server.begin(8080);
  Serial.println("WebServer running on: http://" + WiFi.localIP().toString() + ":8080");

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
  irSendPending = false;
  
  // 创建红外发射任务在核心1运行
  xTaskCreatePinnedToCore(
      irTaskFunction,  
      "IR_Tx_Task",    
      4096+1028,            
      NULL,            
      2,               
      &irTaskHandle,   
      1                
  );

  // 初始状态：检查WiFi连接状态设置灯光
  if (WiFi.status() != WL_CONNECTED) {
    rgbBlinkState = 1;
    rgbBlinkTicker.attach(0.5, blinkGreen); // 未连接则绿灯闪烁
  } else {
    setWS2812BColor(COLOR_OFF); // 已连接则关闭灯光
  }
  
  Serial.println("系统初始化完成，默认启动WiFi模式");
}

// 温湿度数据发送（保持不变）
void sendEnvironmentDataIfNeeded() {
  if (!deviceConnected || !pTxCharacteristic) return;
  
  bool hasTemperatureChanged = abs(envTemperature - lastSentTemperature) >= TEMPERATURE_CHANGE_THRESHOLD;
  bool hasHumidityChanged = abs(enHumidity - lastSentHumidity) >= HUMIDITY_CHANGE_THRESHOLD;
  bool dataChanged = hasTemperatureChanged || hasHumidityChanged;
  
  bool timeExpired = (millis() - lastBleDataSend >= BLE_DATA_SEND_INTERVAL);
  
  if (dataChanged || timeExpired) {
    String tempData = "temp=" + String(envTemperature, 1) + 
                    ";humidity=" + String(enHumidity, 1);
    pTxCharacteristic->setValue(tempData.c_str());
    pTxCharacteristic->notify();
    
    lastBleDataSend = millis();
    lastSentTemperature = envTemperature;
    lastSentHumidity = enHumidity;
    
    Serial.println("发送温湿度数据: " + tempData);
  }
}

// 主循环（保持不变）
void loop() {
  delay(100);
  
  if (requestSwitchToWiFi) {
    requestSwitchToWiFi = false;
    enableWiFi();
  }
  
  if (requestSwitchToBLE) {
    requestSwitchToBLE = false;
    enableBLE();
  }
  
  if (isWiFiEnabled) {
    if (webServerActive) {
      server.handleClient();
    }
    homeSpan.poll();
    checkWiFiConnection();
  } 
  else if (isBLEEnabled) {
    sendEnvironmentDataIfNeeded();
    // BLE连接状态变化处理
    if (deviceConnected != oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
    }
  }
  
  IRrecvDump();
  handleBootButton();
  readSensorData();
  checkWiFiConnection();
} 