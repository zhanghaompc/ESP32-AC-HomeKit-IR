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
// 新增BLE相关库
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// BLE配置
#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// 模式状态变量
bool isWiFiEnabled = true;    // 默认WiFi模式
bool isBLEEnabled = false;
unsigned long pressStartTime = 0;
bool buttonPressed = false;

// BLE相关对象
BLEServer* pBLEServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
// BLECharacteristic* pBLECharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
// 在全局变量区域添加
unsigned long lastBleDataSend = 0;  // 记录上次发送蓝牙数据的时间
const unsigned long BLE_DATA_SEND_INTERVAL = 2500;  // 蓝牙数据发送间隔(2.5秒)

// 原有定义保持不变
#define LED_BLUE 2
#define KEY 0       // BOOT键，用于WiFi/BLE切换
#define KEY1 4
#define KEY2 14

Adafruit_AHTX0 aht;
bool sensorOnline = false;        
unsigned long lastSensorRead = 0; 
float envTemperature = 25.0;  
float acTargetTemp = 26.0;    
float enHumidity = 50.0;     

String lastProtocolName = "COOLIX";  
const uint16_t kIrLed = 4;  
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
Ticker ticker;
WebServer server(8080);



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


// 原有函数声明
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


decode_type_t getProtocolEnumFromString(const String& proto) {
  if (protocolMap.count(proto)) {
    return protocolMap[proto];
  } else {
    return decode_type_t::UNKNOWN;
  }
}

bool updateProtocolFromString(const String& protocolName, decode_type_t& targetProtocol) {
  decode_type_t protoEnum = getProtocolEnumFromString(protocolName);
  if (protoEnum != decode_type_t::UNKNOWN) {
    targetProtocol = protoEnum;
    lastProtocolName = protocolName; // 更新全局协议名称
    Serial.println("更新协议为：" + protocolName);
    return true;
  } else {
    Serial.println("无法识别协议：" + protocolName);
    return false;
  }
}

// 保留并修正MyBLEServerCallbacks
class MyBLEServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("BLE客户端已连接");
  };

  void onDisconnect(BLEServer* pServer) override {  // 增加override确保正确重写
    deviceConnected = false;
    Serial.println("BLE客户端已断开");
    // 断开后重启广告以便重新连接（关键修复）
    pServer->startAdvertising();  // 正确重启广告的方法
  }
};
// 处理红外接收和解码，并返回识别到的协议
String handleIrReceiving() {
  if (irrecv.decode(&results)) {
    String protoName = typeToString(results.decode_type);
    Serial.println("识别到的协议：" + protoName);
    
    // 打印空调相关信息（如果有）
    String description = IRAcUtils::resultAcToString(&results);
    if (description.length()) {
      Serial.println("空调信息：" + description);
    }
    
    irrecv.resume();  // 继续接收下一个值
    return protoName;
  }
  return "";
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("onConnect");
  };

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("onDisconnect");
  }
};
String resStr;
String chipId;
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.empty()) return;

    String command = String(rxValue.c_str());
    Serial.println("收到小程序命令: " + command);

    // 1. 处理电源开关命令（power=on/off）
    if (command.startsWith("power=")) {
      String powerState = command.substring(6);
      ac.next.power = (powerState == "on");
      ac.sendAc();
      Serial.print("空调电源已");
      Serial.println(ac.next.power ? "开启" : "关闭");
      // 回复确认
      pTxCharacteristic->setValue(("power=" + powerState).c_str());
      pTxCharacteristic->notify();
      return;
    }

    // 2. 处理复合控制命令（temp=26;mode=1;speed=3;power=on）
    if (command.startsWith("temp=")) {
      // 解析温度
      int temp = command.substring(5, command.indexOf(';')).toInt();
      command = command.substring(command.indexOf(';') + 1);
      
      // 解析模式
      int mode = command.substring(5, command.indexOf(';')).toInt();
      command = command.substring(command.indexOf(';') + 1);
      
      // 解析风速
      int speed = command.substring(6, command.indexOf(';')).toInt();
      command = command.substring(command.indexOf(';') + 1);
      
      // 解析电源状态
      String powerState = command.substring(6);
      
      // 应用设置
      AC_SET_DATA(temp, speed, mode);
      ac.next.power = (powerState == "on");
      ac.sendAc();
      
      // 回复确认
      pTxCharacteristic->setValue(("已设置:温度=" + String(temp) + "℃ 模式=" + String(mode)).c_str());
      pTxCharacteristic->notify();
      return;
    }

    // 3. 处理协议设置命令（protocol=COOLIX）
    if (command.startsWith("protocol=")) {
      String proto = command.substring(9);
      bool success = updateProtocolFromString(proto, ac.next.protocol);
      if (success) {
        pTxCharacteristic->setValue(("protocol=" + proto).c_str());
      } else {
        pTxCharacteristic->setValue("protocol=invalid");
      }
      pTxCharacteristic->notify();
      return;
    }

    // 4. 处理学习模式命令（learn=start）
    if (command == "learn=start") {
      Serial.println("进入协议学习模式...");
      pTxCharacteristic->setValue("learn=waiting"); // 通知小程序开始等待
      pTxCharacteristic->notify();
      
      digitalWrite(LED_BLUE, HIGH); // 点亮LED指示学习中
      unsigned long startTime = millis();
      String detectedProtocol = "";
      
      // 10秒内等待红外信号
      while (detectedProtocol.isEmpty() && (millis() - startTime < 10000)) {
        detectedProtocol = handleIrReceiving();
        delay(100);
      }
      
      digitalWrite(LED_BLUE, LOW);
      
      if (!detectedProtocol.isEmpty()) {
        updateProtocolFromString(detectedProtocol, ac.next.protocol);
        pTxCharacteristic->setValue(("learn=success:" + detectedProtocol).c_str()); // 匹配小程序解析格式
      } else {
        pTxCharacteristic->setValue("learn=timeout");
      }
      pTxCharacteristic->notify();
      return;
    }

    // 5. 处理状态查询命令（小程序可能发送的隐含命令）
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
// WiFi控制函数
void enableWiFi() {
  if (isWiFiEnabled) return;
  
  Serial.println("开启WiFi...");
  WiFi.mode(WIFI_STA);
  initWifiManager();  // 重新初始化WiFi连接
  server.begin();     // 重启WebServer
  webServerActive = true;
  // homeSpan.resume();  // 恢复HomeSpan运行
  isWiFiEnabled = true;
  Serial.println("WiFi已开启");
}

void disableWiFi() {
  if (!isWiFiEnabled) return;
  
  Serial.println("关闭WiFi...");
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  server.stop();      // 停止WebServer
  webServerActive = false;
  // homeSpan.pause();   // 暂停HomeSpan
  isWiFiEnabled = false;
  Serial.println("WiFi已关闭");
}

void enableBLE() {
  if (isBLEEnabled) return;
  
  Serial.println("切换到BLE模式...");
  BLEDevice::deinit(); // 确保初始化干净
  // BLEDevice::init("ESP32-AC-Control"); // 设备名称与小程序搜索匹配
  BLEDevice::init("ESP32-AC");
  pBLEServer = BLEDevice::createServer();
  pBLEServer->setCallbacks(new MyBLEServerCallbacks());

  // 创建服务（UUID与小程序严格一致）
  BLEService *pService = pBLEServer->createService(BLE_SERVICE_UUID);
  
  // TX特征（ESP32发送数据到小程序）：必须包含NOTIFY权限+BLE2902描述符
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902()); // 小程序接收通知必需
  
  // RX特征（接收小程序命令）：必须包含WRITE权限
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxCharacteristic->setCallbacks(new MyCallbacks()); // 绑定命令处理回调

  pService->start();
  
  // 广播服务UUID（确保小程序能通过服务UUID过滤设备）
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID); // 关键：广播服务UUID
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // 适配iOS
  pAdvertising->setMinPreferred(0x12);
  pBLEServer->startAdvertising();

  isBLEEnabled = true;
  isWiFiEnabled = false;
  Serial.println("BLE初始化完成，等待小程序连接...");
}
void disableBLE() {
  if (!isBLEEnabled) return;
  
  Serial.println("关闭BLE...");
  if (pBLEServer) {
    // 修改为正确的API调用
    pBLEServer->getAdvertising()->stop();
    
    // 使用getConnId()方法获取当前连接ID
    uint16_t connId = pBLEServer->getConnId();
    if (connId != 0) { // 如果有连接
      pBLEServer->disconnect(connId);
    }
  }
  BLEDevice::deinit();
  isBLEEnabled = false;
  Serial.println("BLE已关闭");
}

// 按键处理函数（修改为支持长按短按切换）
void handleBootButton() {
  int keyState = digitalRead(KEY);
  
  if (keyState == LOW) {  // 按键按下
    if (!buttonPressed) {
      buttonPressed = true;
      pressStartTime = millis();
    }
  } else {  // 按键释放
    if (buttonPressed) {
      buttonPressed = false;
      unsigned long pressDuration = millis() - pressStartTime;
      
      if (pressDuration >= 2000) {  // 长按2秒：WiFi->BLE
        Serial.println("检测到长按，切换到BLE模式");
        disableWiFi();
        enableBLE();
      } else {  // 短按：BLE->WiFi
        Serial.println("检测到短按，切换到WiFi模式");
        disableBLE();
        enableWiFi();
      }
    }
  }
}



void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("进入配置模式");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  ticker.attach(0.2, tick);
} 

void tick() {
  int state = digitalRead(LED_BLUE);
  digitalWrite(LED_BLUE, !state);
}

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

void checkWiFiConnection() {
  if (!isWiFiEnabled) return;  // WiFi模式下才检查连接
  
  static unsigned long lastAttemptTime = 0;
  const unsigned long retryInterval = 5000;
  
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttemptTime >= retryInterval) {
      Serial.println("WiFi已断开，尝试重连...");
      WiFi.reconnect();
      lastAttemptTime = millis();
      
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("重连失败，进入配置模式...");
        ticker.attach(0.2, tick);
        initWifiManager();
      }
    }
  } else {
    ticker.detach();
    digitalWrite(LED_BLUE, LOW);
  }
}

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

  server.on("/learn", HTTP_GET, []() {
    Serial.println("开始协议学习...");
    digitalWrite(LED_BLUE, HIGH);
    
    unsigned long startTime = millis();
    String detectedProtocol_web = "";
    
    while (detectedProtocol_web.isEmpty() && (millis() - startTime < 10000)) {
      detectedProtocol_web = handleIrReceiving();
      delay(100);
    }
    
    digitalWrite(LED_BLUE, LOW);
    
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

void key_init() {
  pinMode(LED_BLUE, OUTPUT);
  pinMode(KEY, INPUT_PULLUP);
  pinMode(KEY1, INPUT_PULLUP);
  pinMode(KEY2, INPUT_PULLUP);
}

// 修改按键扫描函数，整合BOOT键切换逻辑
void key_scan() {
  // 处理BOOT键（KEY）的WiFi/BLE切换
  handleBootButton();

  // 保留原有KEY1功能（空调关闭）
  if(digitalRead(KEY1) == LOW) {
    delay(5);
    if(digitalRead(KEY1) == LOW) { 
      updateProtocolFromString(lastProtocolName, ac.next.protocol);
      ac.next.light = true;
      ac.next.power = false;
      Serial.println("发送消息以关闭空调。");
      ac.sendAc();
      digitalWrite(LED_BLUE, !digitalRead(LED_BLUE));
      while(digitalRead(KEY1) == LOW);
    }
  }

  // 保留原有KEY2功能（协议学习）
  if (digitalRead(KEY2) == LOW) {
    delay(5);
    if (digitalRead(KEY2) == LOW) {
      Serial.println("请按下遥控器学习...");
      digitalWrite(LED_BLUE, HIGH);
      
      unsigned long startTime = millis();
      String detectedProtocol = "";
      
      while (detectedProtocol.isEmpty() && (millis() - startTime < 10000)) {
        detectedProtocol = handleIrReceiving();
        delay(100);
      }
      
      if (!detectedProtocol.isEmpty()) {
        updateProtocolFromString(detectedProtocol, ac.next.protocol);
        Serial.println("协议学习成功，当前协议：" + detectedProtocol);
      } else {
        Serial.println("学习超时，未接收到有效信号");
      }
      
      digitalWrite(LED_BLUE, LOW);
      while (digitalRead(KEY2) == LOW);
    }
  }
} 

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

void AC_SET_DATA(int temp, int speed, int mode) {
  ac.next.degrees = temp;
  ac.next.fanspeed = (stdAc::fanspeed_t)speed;
  ac.next.mode = (stdAc::opmode_t)mode;
  ac.next.light = true;
  ac.sendAc();
  Serial.println("设置温度为：" + String(temp) + "，风速为：" + String(speed) + "，模式为：" + String(mode));
}

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
  initWifiManager();  // 默认启动WiFi
  Serial.println("connected...so easy :)");
  ticker.detach();
  digitalWrite(LED_BLUE, HIGH);
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
  digitalWrite(LED_BLUE, LOW);

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
  
  Serial.println("系统初始化完成，默认启动WiFi模式");
}

void loop() {
  delay(100);
  
  // 根据当前模式处理不同任务
  if (isWiFiEnabled) {
    if (webServerActive) {
      server.handleClient();
    }
    homeSpan.poll();
  } else if (isBLEEnabled) {
   // BLE模式下每2.5秒发送一次温湿度数据
    if (deviceConnected && pTxCharacteristic && 
        (millis() - lastBleDataSend >= BLE_DATA_SEND_INTERVAL)) {  // 使用定义的间隔常量
      
      // 格式化数据为小程序可解析的格式（推荐键值对格式）
      String tempData = "temp=" + String(envTemperature, 1) + 
                       ";humidity=" + String(enHumidity, 1);
      pTxCharacteristic->setValue(tempData.c_str());
      pTxCharacteristic->notify();
      
      // 记录本次发送时间
      lastBleDataSend = millis();
      Serial.println("发送温湿度数据: " + tempData);  // 调试用
    }
  }
  
  // 公共任务
  if (Serial2.available() > 0) {
    String receivedCommand = Serial2.readStringUntil('\n');
    receivedCommand.trim();
    handleCommand(receivedCommand);
  }
  
  IRrecvDump();
  readSensorData();
  checkWiFiConnection();
  key_scan();  // 处理按键扫描（包括模式切换）
} 