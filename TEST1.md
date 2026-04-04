//目前可以使用但是有问题 

1.蓝牙和WiFi不能同时使用 
2.温湿度数据没有上传微信小程序
3.微信小程序的回调函数处理有问题 设置的红外数据不准确



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




//定义LED与按键管脚
#define LED_BLUE 2
#define KEY 0
#define KEY1 4
#define KEY2 14

//String chipId;
// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
/*ble*/
BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint8_t txValue = 0;

// 在全局变量区域添加  
Adafruit_AHTX0 aht;
bool sensorOnline = false;        // 传感器状态标志
unsigned long lastSensorRead = 0; // 最后读取时间
float envTemperature = 25.0;  // 环境实际温度（来自AHT20）
float acTargetTemp = 26.0;    // 空调设定温度
float enHumidity = 50.0;     // 当前湿度（可选）

String lastProtocolName = "COOLIX";  // 存储最近一次红外协议字符串，默认设置为COOLIX
const uint16_t kIrLed = 23;  // 用于控制红外LED的ESP GPIO引脚23
const uint16_t kRecvPin = 18;  // 用于接收红外信号的ESP GPIO引脚
const uint16_t kCaptureBufferSize = 1024;  // 捕获缓冲区大小
const uint8_t kTimeout = 50;
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = kTolerance;  // kTolerance通常是25%
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;  // 存储解码结果的地方
IRac ac(kIrLed);  // 创建一个使用指定GPIO发送消息的空调对象

//标志位
bool wifiConnected = false;
bool shouldSaveConfig = false;
bool webServerActive = true;  // 默认为开启 WebServer
Ticker ticker;

//网络端口
WebServer server(8080);

void initWifiManager();
void configModeCallback(WiFiManager *myWiFiManager);
void checkWiFiConnection();
void tick(); 
void AC_SET_DATA(int temp, int speed, int mode);
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
        // bool power = command.substring(6) == "off";
        // //  AC_SET_DATA(temp, speed, mode);
        // // 执行空调控制
        // ac.next.power = power;
        // if (power) {
        //   AC_SET_DATA(temp, speed, mode);
        // }
      }
      // 其他命令...
    }
  }
};
void  ble_server_init() {
    BLEDevice::init("UART Service");

    // Create the BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create the BLE Service
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Create a BLE Characteristic
    pTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_TX,
      BLECharacteristic::PROPERTY_NOTIFY);

    pTxCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE);

    pRxCharacteristic->setCallbacks(new MyCallbacks());

    // Start the service
    pService->start();

    // Start advertising
    // pServer->getAdvertising()->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("Waiting a client connection to notify...");
}
void configModeCallback(WiFiManager *myWiFiManager) {
    Serial.println("Entered config mode");
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
            Serial.println("WiFi connection failed. Opening configuration portal...");
            wifiManager.startConfigPortal("WIFI配置");
        }
    }
}

void checkWiFiConnection() {
    static unsigned long lastAttemptTime = 0;  // 记录上次尝试连接的时间
    const unsigned long retryInterval = 5000;  // 重试间隔时间（5秒）
  
    if (WiFi.status() != WL_CONNECTED) {  // 检查WiFi是否断开连接
      if (millis() - lastAttemptTime >= retryInterval) {  // 如果达到重试间隔时间
        Serial.println("Wi-Fi disconnected. Attempting to reconnect...");
        WiFi.reconnect();  // 尝试重新连接WiFi
        lastAttemptTime = millis();  // 更新最后尝试时间
  
        if (WiFi.status() != WL_CONNECTED) {  // 如果重连失败
          Serial.println("Reconnection failed. Entering AP mode...");
          ticker.attach(0.2, tick);  // 加快LED闪烁频率，表示进入配置模式
          initWifiManager();  // 启动WiFiManager进行重新配置
        }
      }
    } else {  // 如果WiFi连接正常
      ticker.detach();  // 停止LED闪烁
      digitalWrite(LED_BLUE, LOW);  // 关闭LED1
    }
  }

// 修改后的传感器读取函数
void readSensorData() {
  if (sensorOnline && (millis() - lastSensorRead > 5000)) {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    
    if (!isnan(temp.temperature)) {
      envTemperature = temp.temperature;
      enHumidity = humidity.relative_humidity;  // 更新湿度值
      // Serial.printf("传感器读数: 温度=%.1f℃ 湿度=%.1f%%\n", envTemperature, enHumidity);
    }
    lastSensorRead = millis();
  }
}

struct DEV_AC : Service::Thermostat {
      int acPin;
      SpanCharacteristic *currentTemp;    // 环境温度（只读）
      SpanCharacteristic *targetTemp;     // 设定温度（可写）
      SpanCharacteristic *currentHumidity;
      SpanCharacteristic *thermostatMode; // 温控器模式 (0=Off,1=Heat,2=Cool,3=Auto)
      SpanCharacteristic *currentState;
      // 风扇服务相关（仅控制风速）
      SpanCharacteristic *fanSpeed;       // 风速控制 (0-100%)
      SpanCharacteristic *fanDirection;    // 风速方向 (0=Auto,1=Forward,2=Reverse)
      DEV_AC(int pin) : Service::Thermostat() {
      acPin = pin;
      
      // 环境温度传感器
      currentTemp = new Characteristic::CurrentTemperature(envTemperature);
      currentTemp->setRange(0, 50, 0.1);
      
      // 空调目标温度（用户设置）
      targetTemp = new Characteristic::TargetTemperature(26); // 默认26度
      targetTemp->setRange(17.0, 30.0, 1.0);
      
      // 湿度传感器
      currentHumidity = new Characteristic::CurrentRelativeHumidity(50);
      currentHumidity->setRange(0, 100, 1);
      
      // 温控器模式 (HomeKit标准)
      thermostatMode = new Characteristic::TargetHeatingCoolingState(0);  // 0=Off,1=Heat,2=Cool,3=Auto
      currentState = new Characteristic::CurrentHeatingCoolingState(0);
      // 修改风扇服务设置（5档风速）
      Service::Fan *fan = new Service::Fan();
      new Characteristic::Active();
      fanSpeed = new Characteristic::RotationSpeed(0);  // 默认0%（自动风速）
      fanSpeed->setRange(0, 100, 20);  // 5档对应0%,25%,50%,75%,100%
      
      pinMode(acPin, OUTPUT);
  }
      boolean update() {
      // 获取用户设置
      acTargetTemp = targetTemp->getNewVal();
      int hkMode = thermostatMode->getNewVal();  // HomeKit模式 (0=Off,1=Heat,2=Cool,3=Auto)
      int fanSpeedPercent = fanSpeed->getNewVal();
      
      // 5档风速映射
      int acSpeed;
      if(fanSpeedPercent == 0) {
          acSpeed = static_cast<int>(stdAc::fanspeed_t::kAuto);    // 0% → 自动
      } else if(fanSpeedPercent <= 25) {
          acSpeed = static_cast<int>(stdAc::fanspeed_t::kMin);    // 25% → 最低
      } else if(fanSpeedPercent <= 50) {
          acSpeed = static_cast<int>(stdAc::fanspeed_t::kLow);    // 50% → 低速
      } else if(fanSpeedPercent <= 75) {
          acSpeed = static_cast<int>(stdAc::fanspeed_t::kMedium); // 75% → 中速
      } else {
          acSpeed = static_cast<int>(stdAc::fanspeed_t::kHigh);   // 100% → 高速
      }
      // 读取传感器数据
      Serial.println("\n====== HomeKit 控制指令 ======");
      Serial.printf("当前环境: %.1f℃ | 湿度: %.1f%%\n", envTemperature, enHumidity);
      
      // 模式描述（与HomeKit模式顺序一致）
      const char* hkModeNames[] = {"关闭", "制热", "制冷", "自动"};
      const char* speedNames[] = {"自动", "固定", "低速", "中速", "高速"};
      
          int finalSpeed = (hkMode == 3) ? 0 : acSpeed;      // 自动模式强制使用自动风速(0)
          int hkMode_get  ; // 获取HomeKit模式
         // 模式转换
          if(hkMode == 0) {
              hkMode_get = static_cast<int>(stdAc::opmode_t::kOff);  // 关闭
          } else if(hkMode == 1) {
              hkMode_get = static_cast<int>(stdAc::opmode_t::kHeat);  // 制热模式
          } else if(hkMode == 2) {
              hkMode_get = static_cast<int>(stdAc::opmode_t::kCool);  // 制冷模式
          } else if(hkMode == 3) {
              hkMode_get = static_cast<int>(stdAc::opmode_t::kAuto);  // 自动模式 
          }

 
          AC_SET_DATA(acTargetTemp, acSpeed,  hkMode_get); // 直接使用hkMode


      
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
  // 初始化 SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS 初始化失败");
    return;
  }
  
  server.on("/", HTTP_GET, []() {
    File file = SPIFFS.open("/index.html", "r");
    if(!file){
      server.send(404, "text/plain", "文件未找到");
      Serial.println("[网页] 错误:index.html 文件未找到");
      return;
    }

    String html = file.readString();
    file.close();
    server.send(200, "text/html", html);
    Serial.println("[网页] 已发送 HTML 页面");
  });

  server.on("/set", HTTP_GET, []() {
    String temp = server.arg("temp");
    String mode = server.arg("mode");
    String speed = server.arg("speed");
    String protocol = server.arg("protocol");

    // 打印接收到的参数到串口
    Serial.printf("[网页设置] 收到参数 - 温度: %s°C, 模式: %s, 风速: %s, 协议: %s\n", 
                 temp.c_str(), mode.c_str(), speed.c_str(), protocol.c_str());

    int temperature = temp.toInt();
    int modeValue = mode.toInt();
    int speedValue = speed.toInt();

    // 打印转换后的数值
    Serial.printf("[空调控制] 正在设置 - 温度: %d°C, 模式: %d, 风速: %d\n",
                 temperature, modeValue, speedValue);

    // 如果提供了协议参数，更新协议
    if (!protocol.isEmpty()) {
      updateProtocolFromString(protocol, ac.next.protocol);
      Serial.printf("[协议设置] 已更新协议: %s\n", protocol.c_str());
    }

    AC_SET_DATA(temperature, speedValue, modeValue);

    String response = "温度=" + temp + "°C, 模式=" + mode + ", 风速=" + speed;
    if (!protocol.isEmpty()) {
      response += ", 协议=" + protocol;
    }
    server.send(200, "text/plain", response);
    Serial.println("[网页] 已发送响应: " + response);
  });

  // 新增：获取当前协议接口
  server.on("/protocol", HTTP_GET, []() {
    server.send(200, "text/plain", lastProtocolName);
    Serial.printf("[网页] 已发送当前协议: %s\n", lastProtocolName.c_str());
  });

  // 新增：协议学习接口
  server.on("/learn", HTTP_GET, []() {
    Serial.println("开始协议学习...");
    // 点亮LED表示进入学习模式
    digitalWrite(LED_BLUE, HIGH);
    
    // 等待接收红外信号，超时时间为10秒
    unsigned long startTime = millis();
    String detectedProtocol_web = "";
    
    while (detectedProtocol_web.isEmpty() && (millis() - startTime < 10000)) {
      detectedProtocol_web = handleIrReceiving();
      delay(100);
    }
    
    // 关闭LED
    digitalWrite(LED_BLUE, LOW);
    
    if (!detectedProtocol_web.isEmpty()) {
      // 更新协议并显示成功信息
      updateProtocolFromString(detectedProtocol_web, ac.next.protocol);
      server.send(200, "text/plain", "协议学习成功: " + detectedProtocol_web);
      Serial.printf("[协议学习] 成功: %s\n", detectedProtocol_web.c_str());
    } else {
      server.send(200, "text/plain", "学习超时，未接收到有效信号");
      Serial.println("[协议学习] 超时，未接收到有效信号");
    }
  });

  // 切换 WebServer 状态的接口
  server.on("/toggle", HTTP_GET, []() {
    webServerActive = !webServerActive;  // 切换 WebServer 状态
    String status = webServerActive ? "WebServer is active now." : "WebServer is disabled now.";
    server.send(200, "text/plain", status);  // 返回状态信息
    Serial.println(status);  // 打印状态信息
  });
  
  // 温湿度传感器数据接口
  server.on("/sensor", HTTP_GET, []() {
    if (isnan(envTemperature) || isnan(enHumidity)) {
        server.send(500, "application/json", "{\"error\":\"传感器读取失败\"}");
        return;
    }
    
    String json = "{\"temp\":" + String(envTemperature,1) + 
                 ",\"humidity\":" + String(enHumidity,1) + "}";
    server.send(200, "application/json", json);
    // Serial.printf("[网页] 已发送传感器数据: 温度=%.1f°C, 湿度=%.1f%%\n", envTemperature, enHumidity);
  });

  // 电源控制接口
  server.on("/power", HTTP_GET, []() {
    static bool powerState = false;
    powerState = !powerState;

    if (powerState) {
        AC_SET_DATA(26, 3, 1); // 开启空调（默认26度，高速，制冷）
        server.send(200, "text/plain", "空调已开启");
        Serial.println("[空调控制] 空调已开启(默认设置:26°C 高速 制冷）");
    } else {
        ac.next.power = false;  // 设置为关闭空调
        ac.sendAc();  // 发送关闭命令
        server.send(200, "text/plain", "空调已关闭");
        Serial.println("[空调控制] 空调已关闭");
    }
  });
  
  // 启动服务器
  server.begin();
  Serial.println("[网页] HTTP服务器已启动");
}

void key_init()
{
  pinMode(LED_BLUE,OUTPUT);
  //设置KEY引脚为上拉输入
  pinMode(KEY,INPUT_PULLUP);
  pinMode(KEY1,INPUT_PULLUP);
  pinMode(KEY2,INPUT_PULLUP);
}

//按键扫描函数
void key_scan()
{
  // 独立检测KEY按键（控制空调开启）
  if(digitalRead(KEY) == LOW)
  {
    //延时消抖
    delay(5);
    //如果仍为低电平，按键按下
    if(digitalRead(KEY) == LOW)
    { 
      updateProtocolFromString(lastProtocolName, ac.next.protocol);
      ac.next.light = false;  // 尽可能关闭所有LED/灯/显示屏
      ac.next.power = true;  // 设置为打开空调
      Serial.println("发送消息以打开空调。");
      ac.sendAc();  // 让IRac类创建并发送消息
      digitalWrite(LED_BLUE,!digitalRead(LED_BLUE));
      //等待按键松开
      while(digitalRead(KEY) == LOW);
    }
  }

  // 独立检测KEY1按键（控制空调关闭）
  if(digitalRead(KEY1) == LOW)
  {
    //延时消抖
    delay(5);
    //如果仍为低电平，按键按下
    if(digitalRead(KEY1) == LOW)
    {
      updateProtocolFromString(lastProtocolName, ac.next.protocol);
      ac.next.light = true;  // 尽可能关闭所有LED/灯/显示屏
      ac.next.power = false;  // 设置为关闭空调
      Serial.println("发送消息以关闭空调。");
      ac.sendAc();  // 让IRac类创建并发送消息
      digitalWrite(LED_BLUE,!digitalRead(LED_BLUE));
      //等待按键松开
      while(digitalRead(KEY1) == LOW);
    }
  }

    // 学习模式按键处理（GPIO15）
  if (digitalRead(KEY2) == LOW) {
    // 延时消抖
    delay(5);
    if (digitalRead(KEY2) == LOW) {
      Serial.println("请按下遥控器学习...");
      digitalWrite(LED_BLUE, HIGH);  // 点亮LED表示进入学习模式
      
      // 等待接收红外信号，超时时间为10秒
      unsigned long startTime = millis();
      String detectedProtocol = "";
      
      while (detectedProtocol.isEmpty() && (millis() - startTime < 10000)) {
         detectedProtocol = handleIrReceiving();
        delay(100);
      }
      
      if (!detectedProtocol.isEmpty()) {
        // 更新协议并显示成功信息
        updateProtocolFromString(detectedProtocol, ac.next.protocol);
        Serial.println("协议学习成功，当前协议：" + detectedProtocol);
      } else {
        Serial.println("学习超时，未接收到有效信号");
      }
      
      digitalWrite(LED_BLUE, LOW);  // 关闭LED
      // 等待按键松开
      while (digitalRead(KEY2) == LOW);
    }
  }
} 

 void handleCommand(String command) {
     if (command == "off") {
      ac.next.power = false;  // 设置为关闭空调
      Serial.println("关闭空调。");
      ac.sendAc();  // 让IRac类创建并发送消息
    }
    else if (command == "on") {
      ac.next.power = true;  // 设置为打开空调
      Serial.println("打开空调。");
      ac.sendAc();  // 让IRac类创建并发送消息
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
    else {
        // Serial.println("未知命令: " + command);
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

// 修改后的AC_SET_DATA函数 - 移除了协议更新
void AC_SET_DATA(int temp, int speed, int mode) {
    ac.next.degrees = temp;  // 设置温度
    ac.next.fanspeed = (stdAc::fanspeed_t)speed;  // 设置风速
    ac.next.mode = (stdAc::opmode_t)mode;  // 设置模式
    ac.next.light = true;  // 尽可能关闭所有LED/灯/显示屏
    ac.sendAc();  // 发送空调指令
    Serial.println("设置温度为：" + String(temp) + "，风速为：" + String(speed) + "，模式为：" + String(mode));
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);

  while (!Serial) delay(50);
   
  // 执行低级别的健全性检查
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
  digitalWrite(LED_BLUE, HIGH);
  Serial.println("WiFi Connected: " + WiFi.localIP().toString());
  ble_server_init();

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
  
  // 初始化 AHT20
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

  // 设置默认协议
  updateProtocolFromString(lastProtocolName, ac.next.protocol);
  
  // 设置空调初始状态
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
  
  Serial.println("尝试通过按键打开和关闭空调...");
}
String readString;
void loop() {
  delay(100);
  if (Serial2.available() > 0) {
    String receivedCommand = Serial2.readStringUntil('\n');
    receivedCommand.trim();
    handleCommand(receivedCommand);
  }
  
  IRrecvDump();
  
  if (webServerActive) {
    server.handleClient();
  }
  
  homeSpan.poll();
  readSensorData();
  checkWiFiConnection();
  key_scan();


    if (deviceConnected) {
    //        pTxCharacteristic->setValue(&txValue, 1);
    //        pTxCharacteristic->notify();
    //        txValue++;
    //    delay(10); // bluetooth stack will go into congestion, if too many packets are sent
  }
  while (Serial.available() > 0) {
    if (deviceConnected) {
      delay(3);
      readString += Serial.read();
      pTxCharacteristic->setValue(chipId.c_str());
      //      pTxCharacteristic->setValue((uint32_t)ESP.getEfuseMac());
      pTxCharacteristic->notify();
      Serial.println(chipId);
    }
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);                   // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising();  // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }


}
