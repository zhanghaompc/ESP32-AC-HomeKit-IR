#include <Arduino.h>
#include <IRremoteESP8266.h>
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
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_task_wdt.h>

// ============= 函数声明 =============
void tick();
void initWifiManager();
void checkWiFiConnection();
void readSensorData();
void key_init();
void key_scan();
void handleCommand(String command);
void IRrecvDump();
void AC_SET_DATA(int temp, int speed, int mode);
String handleIrReceiving();
decode_type_t getProtocolEnumFromString(const String& proto);
bool updateProtocolFromString(const String& protocolName, decode_type_t& targetProtocol);
void Web_set();
void ble_server_init();

// ============= 全局变量 =============
// 定义LED与按键管脚
#define LED_BLUE 2
#define KEY 0
#define KEY1 4
#define KEY2 14

// BLE UUID
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

WiFiManager wifiManager;  // WiFiManager全局对象
Adafruit_AHTX0 aht;
bool sensorOnline = false;        // 传感器状态标志
unsigned long lastSensorRead = 0; // 最后读取时间
float envTemperature = 25.0;      // 环境实际温度
float acTargetTemp = 26.0;        // 空调设定温度
float enHumidity = 50.0;          // 当前湿度

String lastProtocolName = "COOLIX";  // 存储最近一次红外协议字符串
const uint16_t kIrLed = 23;          // 红外LED引脚
const uint16_t kRecvPin = 18;        // 红外接收引脚
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 50;
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = kTolerance;  // kTolerance通常是25%
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;  // 存储解码结果的地方
IRac ac(kIrLed);         // 创建一个使用指定GPIO发送消息的空调对象

// 标志位
bool wifiConnected = false;
bool shouldSaveConfig = false;
bool webServerActive = true;  // 默认为开启 WebServer
Ticker ticker;

// 网络端口
WebServer server(8080);

// 红外学习模式状态
bool learningMode = false;
unsigned long learnStartTime = 0;
String learnedProtocol = "";

// BLE 相关
BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint8_t txValue = 0;

// 协议映射表
const std::pair<const char*, decode_type_t> protocolMap[] = {
  {"UNKNOWN", UNKNOWN},
  {"RC5", RC5},
  {"RC6", RC6},
  {"NEC", NEC},
  {"SONY", SONY},
  {"PANASONIC", PANASONIC},
  {"JVC", JVC},
  {"SAMSUNG", SAMSUNG},
  {"COOLIX", COOLIX},
  {"DAIKIN", DAIKIN},
  {"WHYNTER", WHYNTER},
  {"AIWA_RC_T501", AIWA_RC_T501},
  {"LG", LG},
  {"SANYO", SANYO},
  {"MITSUBISHI", MITSUBISHI},
  {"DISH", DISH},
  {"SHARP", SHARP},
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

// ============= BLE 回调 =============
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("BLE connected");
  };

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("BLE disconnected");
  }
};

String resStr;
String chipId;

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      String command = String(rxValue.c_str());
      
      // 解析协议设置
      if (command.startsWith("protocol=")) {
        String proto = command.substring(9);
        updateProtocolFromString(proto, ac.next.protocol);
      }
      // 解析空调控制命令
      else if (command.startsWith("temp=")) {
        // 提取参数示例：temp=26;mode=1;speed=2;power=on
        int ble_temp = command.substring(5, command.indexOf(';')).toInt();
        command = command.substring(command.indexOf(';') + 1);
        
        int ble_mode = command.substring(5, command.indexOf(';')).toInt();
        command = command.substring(command.indexOf(';') + 1);
        
        int ble_speed = command.substring(6, command.indexOf(';')).toInt();
        command = command.substring(command.indexOf(';') + 1);
        
        AC_SET_DATA(ble_temp, ble_speed, ble_mode);
      }
    }
  }
};

// ============= WiFi 相关 =============
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println("AP IP: " + WiFi.softAPIP().toString());
  Serial.println("SSID: " + myWiFiManager->getConfigPortalSSID());
  ticker.attach(0.2, tick);
} 

void tick() {
  digitalWrite(LED_BLUE, !digitalRead(LED_BLUE));
}

void initWifiManager() {
  wifiManager.setTitle("永远相信美好的事物即将发生");
  wifiManager.setTimeout(60);
  wifiManager.setAPCallback(configModeCallback);

  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiManager.autoConnect("WIFI配置")) {
      Serial.println("Opening configuration portal...");
      wifiManager.startConfigPortal("WIFI配置");
    }
  }
}

void checkWiFiConnection() {
  static unsigned long lastAttemptTime = 0;
  const unsigned long retryInterval = 5000;
  
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttemptTime >= retryInterval) {
      Serial.println("Wi-Fi disconnected. Reconnecting...");
      WiFi.reconnect();
      lastAttemptTime = millis();

      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Reconnection failed. Entering AP mode...");
        ticker.attach(0.2, tick);
        initWifiManager();
      }
    }
  } else {
    ticker.detach();
    digitalWrite(LED_BLUE, LOW);
  }
}

// ============= 传感器处理 =============
void readSensorData() {
  if (millis() - lastSensorRead > 5000) {
    if (!sensorOnline) {
      // 尝试重新初始化传感器
      sensorOnline = aht.begin();
      if (sensorOnline) {
        Serial.println("AHT20 reinitialized successfully");
      }
    }
    
    if (sensorOnline) {
      sensors_event_t humidity, temp;
      if (aht.getEvent(&humidity, &temp)) {
        if (!isnan(temp.temperature)) {
          envTemperature = temp.temperature;
          enHumidity = humidity.relative_humidity;
          Serial.printf("Sensor: %.1f°C, %.1f%%\n", envTemperature, enHumidity);
        } else {
          Serial.println("Invalid sensor reading");
          sensorOnline = false;
        }
      } else {
        Serial.println("Failed to read sensor");
        sensorOnline = false;
      }
    } else {
      Serial.println("Sensor offline, attempting recovery...");
      sensorOnline = aht.begin(); // 尝试重新连接
    }
    lastSensorRead = millis();
  }
}

// ============= HomeKit 设备 =============
struct DEV_AC : Service::Thermostat {
  int acPin;
  SpanCharacteristic *currentTemp;    // 环境温度（只读）
  SpanCharacteristic *targetTemp;     // 设定温度（可写）
  SpanCharacteristic *currentHumidity;
  SpanCharacteristic *thermostatMode; // 温控器模式 (0=Off,1=Heat,2=Cool,3=Auto)
  SpanCharacteristic *currentState;
  SpanCharacteristic *fanSpeed;       // 风速控制 (0-100%)
  
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
    
    // 风扇服务
    Service::Fan *fan = new Service::Fan();
    new Characteristic::Active();
    fanSpeed = new Characteristic::RotationSpeed(0);
    fanSpeed->setRange(0, 100, 20);  // 5档风速
    
    pinMode(acPin, OUTPUT);
  }
  
  boolean update() {
    acTargetTemp = targetTemp->getNewVal();
    int hkMode = thermostatMode->getNewVal();
    int fanSpeedPercent = fanSpeed->getNewVal();
    
    // 风速映射
    int acSpeed;
    if(fanSpeedPercent == 0) acSpeed = static_cast<int>(stdAc::fanspeed_t::kAuto);
    else if(fanSpeedPercent <= 25) acSpeed = static_cast<int>(stdAc::fanspeed_t::kMin);
    else if(fanSpeedPercent <= 50) acSpeed = static_cast<int>(stdAc::fanspeed_t::kLow);
    else if(fanSpeedPercent <= 75) acSpeed = static_cast<int>(stdAc::fanspeed_t::kMedium);
    else acSpeed = static_cast<int>(stdAc::fanspeed_t::kHigh);
    
    // 模式转换
    int acMode;
    if(hkMode == 0) acMode = static_cast<int>(stdAc::opmode_t::kOff);
    else if(hkMode == 1) acMode = static_cast<int>(stdAc::opmode_t::kHeat);
    else if(hkMode == 2) acMode = static_cast<int>(stdAc::opmode_t::kCool);
    else if(hkMode == 3) acMode = static_cast<int>(stdAc::opmode_t::kAuto);
    
    // 发送空调指令
    AC_SET_DATA(acTargetTemp, acSpeed, acMode);
    
    Serial.printf("\nHomeKit Control: %.1f°C | %d%% fan | Mode %d\n", 
                 acTargetTemp, fanSpeedPercent, hkMode);
    return true;  
  }
  
  void loop() {
    if (currentTemp->timeVal() > 2000) {
      currentTemp->setVal(envTemperature);
      currentHumidity->setVal(enHumidity);
    }
  }
};

// ============= Web 服务器 =============
void Web_set() {
  if(!SPIFFS.begin(true)) {
    Serial.println("SPIFFS init failed");
    return;
  }
  
  server.on("/", HTTP_GET, []() {
    File file = SPIFFS.open("/index.html", "r");
    if(!file) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });

  server.on("/set", HTTP_GET, []() {
    String temp = server.arg("temp");
    String mode = server.arg("mode");
    String speed = server.arg("speed");
    String protocol = server.arg("protocol");

    Serial.printf("Web control: temp=%s, mode=%s, speed=%s, proto=%s\n", 
                 temp.c_str(), mode.c_str(), speed.c_str(), protocol.c_str());

    if (!protocol.isEmpty()) {
      updateProtocolFromString(protocol, ac.next.protocol);
    }

    AC_SET_DATA(temp.toInt(), speed.toInt(), mode.toInt());
    server.send(200, "text/plain", "OK");
  });

  server.on("/protocol", HTTP_GET, []() {
    server.send(200, "text/plain", lastProtocolName);
  });

  server.on("/learn", HTTP_GET, []() {
    Serial.println("Starting protocol learning...");
    learningMode = true;
    learnStartTime = millis();
    digitalWrite(LED_BLUE, HIGH);
    server.send(200, "text/plain", "Learning mode started");
  });

  server.on("/toggle", HTTP_GET, []() {
    webServerActive = !webServerActive;
    String status = webServerActive ? "WebServer active" : "WebServer disabled";
    server.send(200, "text/plain", status);
  });
  
  server.on("/sensor", HTTP_GET, []() {
    String json = "{\"temp\":" + String(envTemperature,1) + 
                 ",\"humidity\":" + String(enHumidity,1) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/power", HTTP_GET, []() {
    static bool powerState = false;
    powerState = !powerState;

    if (powerState) {
        AC_SET_DATA(26, 3, 1);
        server.send(200, "text/plain", "AC ON");
    } else {
        ac.next.power = false;
        ac.sendAc();
        server.send(200, "text/plain", "AC OFF");
    }
  });
  
  server.begin();
  Serial.println("HTTP server started");
}

// ============= 按键处理 =============
void key_init() {
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);
  pinMode(KEY, INPUT_PULLUP);
  pinMode(KEY1, INPUT_PULLUP);
  pinMode(KEY2, INPUT_PULLUP);
}

void key_scan() {
  static unsigned long lastKeyPress = 0;
  
  // 防抖处理
  if (millis() - lastKeyPress < 200) return;
  
  if (digitalRead(KEY) == LOW) {
    lastKeyPress = millis();
    updateProtocolFromString(lastProtocolName, ac.next.protocol);
    ac.next.power = true;
    ac.sendAc();
    digitalWrite(LED_BLUE, HIGH);
    delay(100);
    digitalWrite(LED_BLUE, LOW);
    Serial.println("AC ON via key");
  }

  if (digitalRead(KEY1) == LOW) {
    lastKeyPress = millis();
    updateProtocolFromString(lastProtocolName, ac.next.protocol);
    ac.next.power = false;
    ac.sendAc();
    digitalWrite(LED_BLUE, HIGH);
    delay(100);
    digitalWrite(LED_BLUE, LOW);
    Serial.println("AC OFF via key");
  }

  if (digitalRead(KEY2) == LOW) {
    lastKeyPress = millis();
    learningMode = true;
    learnStartTime = millis();
    digitalWrite(LED_BLUE, HIGH);
    Serial.println("Learning mode started via key");
  }
} 

// ============= 红外处理 =============
String handleIrReceiving() {
  if (irrecv.decode(&results)) {
    String protoName = typeToString(results.decode_type);
    Serial.println("Detected protocol: " + protoName);
    irrecv.resume();
    return protoName;
  }
  return "";
}

void IRrecvDump() {
  if (irrecv.decode(&results)) {
    Serial.println(resultToHumanReadableBasic(&results));
    irrecv.resume();
  }
}

decode_type_t getProtocolEnumFromString(const String& proto) {
  for (auto& entry : protocolMap) {
    if (proto.equalsIgnoreCase(entry.first)) {
      return entry.second;
    }
  }
  return decode_type_t::UNKNOWN;
}

bool updateProtocolFromString(const String& protocolName, decode_type_t& targetProtocol) {
  decode_type_t protoEnum = getProtocolEnumFromString(protocolName);
  if (protoEnum != decode_type_t::UNKNOWN) {
    targetProtocol = protoEnum;
    lastProtocolName = protocolName;
    Serial.println("Protocol updated: " + protocolName);
    return true;
  } else {
    Serial.println("Unknown protocol: " + protocolName);
    return false;
  }
}

void AC_SET_DATA(int temp, int speed, int mode) {
  // 视觉反馈
  digitalWrite(LED_BLUE, HIGH);
  delay(50);
  digitalWrite(LED_BLUE, LOW);
  
  ac.next.degrees = temp;
  ac.next.fanspeed = (stdAc::fanspeed_t)speed;
  ac.next.mode = (stdAc::opmode_t)mode;
  ac.next.light = true;
  ac.next.power = true;
  ac.sendAc();
  
  Serial.printf("AC set: %d°C, speed %d, mode %d\n", temp, speed, mode);
}

void handleCommand(String command) {
  if (command == "off") {
    ac.next.power = false;
    ac.sendAc();
    Serial.println("AC OFF via command");
  }
  else if (command == "on") {
    ac.next.power = true;
    ac.sendAc();
    Serial.println("AC ON via command");
  } 
}

void ble_server_init() {
  BLEDevice::init("Smart AC Controller");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising started");
}

// ============= 主程序 =============
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);
  while (!Serial) delay(50);
  
  // 初始化看门狗
  esp_task_wdt_init(10, true);
  
  key_init();
  initWifiManager();
  Serial.println("WiFi connected: " + WiFi.localIP().toString());
  digitalWrite(LED_BLUE, HIGH);
  
  ble_server_init();
  
  // 初始化红外
  Serial.printf("\nIR Receiver on pin %d\n", kRecvPin);
  irrecv.enableIRIn();
  
  // HomeSpan 设置
  homeSpan.setWifiCredentials(WiFi.SSID().c_str(), WiFi.psk().c_str());
  homeSpan.setApTimeout(300);
  homeSpan.enableAutoStartAP();
  homeSpan.setPairingCode("11122333");
  homeSpan.begin(Category::AirConditioners, "Smart AC");
  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new DEV_AC(15);
  
  // 初始化 SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS init failed");
  }
  
  // 初始化传感器
  sensorOnline = aht.begin();
  if (sensorOnline) {
    Serial.println("AHT20 initialized");
  } else {
    Serial.println("AHT20 not found");
  }
  
  Web_set();
  Serial.println("Web server started on port 8080");
  digitalWrite(LED_BLUE, LOW);

  // 设置默认空调状态
  updateProtocolFromString(lastProtocolName, ac.next.protocol);
  ac.next.model = 1;
  ac.next.celsius = true;
  ac.next.degrees = 25;
  ac.next.fanspeed = stdAc::fanspeed_t::kMedium;
  ac.next.power = true;
  
  Serial.println("Setup completed");
}

void loop() {
  // 重置看门狗
  esp_task_wdt_reset();
  
  // 处理红外学习状态机
  if (learningMode) {
    if (millis() - learnStartTime > 10000) { // 10秒超时
      learningMode = false;
      digitalWrite(LED_BLUE, LOW);
      Serial.println("Learning timeout");
    } else {
      String proto = handleIrReceiving();
      if (!proto.isEmpty()) {
        learningMode = false;
        digitalWrite(LED_BLUE, LOW);
        if (updateProtocolFromString(proto, ac.next.protocol)) {
          Serial.println("Learning success: " + proto);
        }
      }
    }
  }

  // 处理串口命令
  if (Serial2.available() > 0) {
    String receivedCommand = Serial2.readStringUntil('\n');
    receivedCommand.trim();
    handleCommand(receivedCommand);
  }
  
  // 红外接收处理
  IRrecvDump();
  
  // 处理Web服务器
  if (webServerActive) {
    server.handleClient();
  }
  
  // HomeSpan轮询
  homeSpan.poll();
  
  // 读取传感器数据
  readSensorData();
  
  // 检查WiFi连接
  checkWiFiConnection();
  
  // 按键扫描
  key_scan();

  // BLE数据传输
  if (deviceConnected && Serial.available()) {
    String data = Serial.readStringUntil('\n');
    pTxCharacteristic->setValue(data.c_str());
    pTxCharacteristic->notify();
  }

  // BLE连接管理
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("BLE restart advertising");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  // 小延迟防止过载
  delay(10);
}