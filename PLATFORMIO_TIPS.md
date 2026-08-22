# PlatformIO 使用技巧（ESP32 空调控制器项目专用）

## 一、常用命令速查

```bash
# 编译某个环境
pio run -e esp32dev
pio run -e esp32_ble

# 编译并上传（指定端口）
pio run -e esp32dev -t upload --upload-port COM8

# 打开串口监视器
pio device monitor -p COM8 -b 115200

# 清理编译缓存（遇到“改了代码但没生效”时用）
pio run -t clean

# 整片擦除 Flash（启动循环 / 固件损坏时用，擦完重新上传）
pio run -t erase

# 查看编译详细输出（定位报错）
pio run -e esp32dev -v
```

## 二、双环境说明

本项目一套源码，靠 `build_flags` 里的宏区分两个固件版本：

| 环境 | 说明 | 关键宏 |
| --- | --- | --- |
| `esp32dev` | 苹果版：BLE + WiFi + HomeKit | 无 `BLE_ONLY` |
| `esp32_ble` | 安卓纯 BLE 版：无 WiFi/HomeKit | `-D BLE_ONLY` |

- 同时编译两个环境：`pio run -e esp32dev -e esp32_ble`
- **注意**：命令里同时出现多环境 + `-t upload` 时，PlatformIO 会把所有环境依次上传，最后上传的覆盖前面的。只烧一个版本时，只写对应的 `-e`。

## 三、build_flags 技巧

```ini
build_flags = 
	-D DEBUG_LOG                  ; 定义宏（等价于 #define DEBUG_LOG）
	-D ARDUINO_RUNNING_CORE=0     ; 带值的宏（主循环跑核心 0）
	-include "$PROJECT_DIR/src/core_config.h"  ; 自动包含头文件
```

- 只想给某个环境加开关，就加在那个 `[env:...]` 下，互不影响。
- 调试日志、核心分配这类配置建议集中放一个头文件，用 `-include` 管理。
- 依赖版本尽量锁死（如 `crankyoldgit/IRremoteESP8266@2.8.6`），避免自动升级导致行为变化。

## 四、常见问题排查

- **烧录端口打不开**：设备没插好或端口被占用。重新插拔 USB，或用 `pio device list` 查看实际端口。
- **改了代码但行为没变**：先 `pio run -t clean` 再编译。
- **启动循环 / invalid header**：烧录地址错误或固件损坏，`pio run -t erase` 后重新上传。
- **Flash 占用过高**：编译输出末尾会显示 RAM/Flash 百分比，超 100% 会报错。安卓版约 50.5%，余量充足。
- **不要提交 `.pio`、`build/` 到 Git**：`.gitignore` 已配置好。

## 五、效率技巧

- VS Code 装 PlatformIO 插件后，底部状态栏可直接点 ✅ 编译 / ➡ 上传 / 🔌 串口监视。
- 用 `pio device monitor -p COM8 -b 115200` 看设备日志（本项目日志默认开启，`src/Debug.h` 的 `#define DEBUG_LOG` 可一键关闭）。
- 报错时先看最后的 `error:` 行，定位更快。
- 烧录前确认 VS Code 底部选中的是 `esp32dev` 还是 `esp32_ble`，避免烧错版本。
