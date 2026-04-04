#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ======================== 配置项（需修改为你的WiFi信息） ========================
const char* WIFI_SSID     = "你的WiFi名称";  // 替换为实际WiFi名称
const char* WIFI_PASSWORD = "你的WiFi密码";  // 替换为实际WiFi密码

// 阿里云NTP服务器（推荐，无需解析JSON，更稳定）
#define NTP_SERVER      "ntp.aliyun.com"
#define GMT_OFFSET_SEC  8 * 3600  // 东八区（GMT+8）
#define DAYLIGHT_OFFSET_SEC 0    // 无夏令时

// 阿里云HTTP时间接口（备用方案，返回毫秒级时间戳）
#define ALIYUN_TIME_API "http://api.m.taobao.com/rest/api3.do?api=mtop.common.getTimestamp"

// ======================== 核心函数声明 ========================
void connectWiFi();                  // 连接WiFi
void syncAliyunTimeViaNTP();         // 通过NTP同步阿里云时间（推荐）
void getAliyunTimeViaHTTP();         // 通过HTTP获取阿里云时间（备用）

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n====== ESP32 连接阿里云获取当前时间 ======");

  // 1. 连接WiFi
  connectWiFi();

  // 2. 优先使用NTP同步阿里云时间（更简单、稳定）
  Serial.println("\n--- 方式1：阿里云NTP时间同步 ---");
  syncAliyunTimeViaNTP();

  // 3. 备用：通过HTTP接口获取时间（可选）
  Serial.println("\n--- 方式2：阿里云HTTP接口获取时间 ---");
  getAliyunTimeViaHTTP();
}

void loop() {
  // 每隔10秒打印一次当前时间
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 10000) {
    lastPrint = millis();
    
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("当前时间（10秒刷新）：%s\n", timeStr);
  }
  delay(100);
}

// ======================== 核心函数实现 ========================
/**
 * 连接WiFi
 */
void connectWiFi() {
  Serial.printf("正在连接WiFi: %s ...\n", WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // 等待WiFi连接（最多10秒）
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 10) {
    delay(1000);
    Serial.print(".");
    retry++;
  }

  // 连接结果判断
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi连接成功！");
    Serial.print("IP地址：");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi连接失败，请检查账号密码！");
    while (1) delay(1000); // 连接失败则卡死，等待重启
  }
}

/**
 * 方式1：通过NTP同步阿里云时间（推荐）
 * 优点：无需解析JSON，系统级同步，稳定精准
 */
void syncAliyunTimeViaNTP() {
  // 配置NTP服务器和时区
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // 等待时间同步完成（最多10秒）
  int retry = 0;
  while (timeinfo.tm_year < (2020 - 1900) && retry < 10) {
    delay(1000);
    now = time(nullptr);
    localtime_r(&now, &timeinfo);
    retry++;
    Serial.printf("等待NTP同步...(%d/10)\n", retry);
  }

  if (retry >= 10) {
    Serial.println("NTP同步超时！");
    return;
  }

  // 格式化并打印时间
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.printf("阿里云NTP时间：%s\n", timeStr);
}

/**
 * 方式2：通过HTTP接口获取阿里云时间（备用）
 * 优点：无需依赖NTP服务，仅需HTTP访问；缺点：需解析JSON
 */
void getAliyunTimeViaHTTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi未连接，跳过HTTP时间获取");
    return;
  }

  HTTPClient http;
  http.begin(ALIYUN_TIME_API);
  http.setTimeout(5000); // 5秒超时

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP请求失败，状态码：%d\n", httpCode);
    http.end();
    return;
  }

  // 解析JSON获取时间戳
  String payload = http.getString();
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload);
  
  if (error) {
    Serial.printf("JSON解析失败：%s\n", error.c_str());
    http.end();
    return;
  }

  // 提取毫秒级时间戳并转换为本地时间
  String timestampStr = doc["data"]["t"] | "";
  if (timestampStr.isEmpty()) {
    Serial.println("未提取到时间戳");
    http.end();
    return;
  }

  time_t timestamp = timestampStr.toInt() / 1000; // 转换为秒级
  struct tm timeinfo;
  localtime_r(&timestamp, &timeinfo);

  // 格式化打印
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.printf("阿里云HTTP时间：%s（时间戳：%lu）\n", timeStr, timestamp);
  
  http.end();
}