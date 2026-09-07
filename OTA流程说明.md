# OTA 流程说明

本文说明当前项目的 OTA 发布和升级流程，分为 ESP32 和 ESP8266 两部分。

## 1. 整体思路

- ESP32：按固件环境分别发布，使用对应的 `ota_<env>.json` 清单。
- ESP8266：不再走 MQTT 分包下载，改为直接 HTTP 下载固件。

## 2. ESP32 发布流程

ESP32 目前有三个环境：

- `esp32dev`
- `esp32_wifi`
- `esp32_ble`

每个环境都会生成自己的固件和自己的清单文件：

- `firmware/esp32dev.bin`
- `firmware/esp32_wifi.bin`
- `firmware/esp32_ble.bin`
- `firmware/ota_esp32dev.json`
- `firmware/ota_esp32_wifi.json`
- `firmware/ota_esp32_ble.json`

发布时会做这些事：

1. 更新 `src/DeviceConfig.h` 里的 `FW_VERSION`
2. 编译指定环境
3. 复制 `.pio/build/<env>/firmware.bin` 到 `firmware/<env>.bin`
4. 生成 `firmware/ota_<env>.json`
5. 提交到 `master`
6. 推送到 GitHub

## 3. ESP32 设备升级流程

设备启动后会：

1. 读取当前环境名 `OTA_ENV_NAME`
2. 去拉取对应清单 `ota_<env>.json`
3. 读取清单里的 `version` 和 `url`
4. 比较本地版本和远端版本
5. 如果有新版本：
   - `BLE` 模式会直接升级
   - `WiFi` 模式会先返回 `ota=found`
   - 用户确认后再执行 `ota=go`
6. 下载完成后重启

## 4. ESP8266 发布流程

ESP8266 现在改成：

- MQTT 只负责控制
- 真正的固件下载走 HTTP

它的流程是：

1. 检查 `FW_VERSION`
2. 从 `ota.json` 读取远端版本和下载地址
3. 如果有新版本，设备开始 HTTP 下载
4. 主循环里分片写入 Flash
5. 下载完成后重启

## 5. ESP8266 旧问题的修复

之前 ESP8266 的 OTA 慢，主要是因为：

- 走 MQTT 分包传输
- 逻辑比较重
- 对网络和主循环占用比较大

现在已经改成 HTTP 直下，速度会稳定很多。

## 6. 手动发布命令

ESP32 发布例如：

```powershell
powershell -ExecutionPolicy Bypass -File .\publish.ps1 esp32_wifi 1.4.9
```

也可以替换成：

- `esp32dev`
- `esp32_ble`

## 7. 你实际测试时怎么看

- 先确认设备当前版本
- 再发布一个更高版本
- 设备检查到新版本后应能看到：
  - ESP32 WiFi：`ota=found`
  - ESP32 BLE：直接升级
  - ESP8266：直接进入 HTTP 下载

## 8. 关键文件

- `src/OtaManager.cpp`
- `src/OtaManager.h`
- `src/BleManager.cpp`
- `src/WifiManagerEx.cpp`
- `src/main.cpp`
- `publish.ps1`
- `src/DeviceConfig.h`

