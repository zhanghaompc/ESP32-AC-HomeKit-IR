# OTA 升级说明

当前公开仓库只保留设备端 OTA 实现和固件清单，不包含自动提交、自动推送 GitHub 或自动测试设备的发布脚本。

## 设备升级流程

推荐使用 `esp32_wifi` 环境。它采用双应用分区，设备升级时先把新固件写入备用分区，写入完成后重启切换；下载失败不会覆盖当前运行的固件。

设备通过 MQTT 网页面板执行升级：

1. 网页面板发送 `ota=check`。
2. 设备读取 `firmware/ota_esp32_wifi.json`，比较远端版本与本地 `FW_VERSION`。
3. 发现新版本后返回 `ota=found <version>`，等待用户确认。
4. 用户确认后发送 `ota=go`，设备异步下载固件并通过 MQTT 发布 `ota=progress <percent>`。
5. 写入完成后设备返回 `ota=ok` 并自动重启。

升级失败时会返回 `ota=fail:<reason>`，设备保留当前固件继续运行。

## 公开仓库中的 OTA 文件

| 文件 | 用途 |
| --- | --- |
| `firmware/esp32_wifi.bin` | WiFi 主线固件二进制文件 |
| `firmware/ota_esp32_wifi.json` | 版本号和固件下载地址清单 |
| `src/OtaManager.cpp` | 版本检查、下载、进度和写入逻辑 |
| `src/OtaManager.h` | OTA 管理接口和状态定义 |
| `huge_app_ota.csv` | 双应用分区表 |

清单格式示例：

```json
{
  "version": "1.5.31",
  "url": {
    "esp32_wifi": "https://example.com/firmware/esp32_wifi.bin"
  }
}
```

## 维护者发布步骤

发布新版本时，请由维护者在本地完成以下检查后，再通过正常的 Git 工作流提交固件和清单：

1. 修改 `src/DeviceConfig.h` 中的 `FW_VERSION`。
2. 使用 PlatformIO 编译 `esp32_wifi`。
3. 使用真实设备验证配网、MQTT、红外发射、HomeKit 和 OTA。
4. 更新 `firmware/esp32_wifi.bin` 与 `firmware/ota_esp32_wifi.json`。
5. 确认下载地址可访问，且版本号高于设备当前版本。

请不要把账号、Token、私钥、设备局域网地址或个人自动化脚本提交到公开仓库。

## 注意事项

- OTA 依赖设备已连接 WiFi 和 MQTT。
- 下载期间不要断电；如果设备异常重启，ESP32 的 OTA 分区机制会尝试启动上一个可用固件。
- `esp32dev` 和 `esp32_ble` 属于兼容 BLE 的历史构建环境，当前公开发布主线仍为 `esp32_wifi`。
