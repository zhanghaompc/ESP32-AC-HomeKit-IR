# ESP32 智能空调红外控制器

基于 ESP32 的开源空调控制器。设备通过红外模拟原装遥控器，可使用 **MQTT 网页面板或 Apple HomeKit** 控制空调，并提供温湿度监测、定时任务、红外协议学习和在线固件升级。

当前主线是 `esp32_wifi`：上电后通过浏览器完成 WiFi 配置，随后使用 MQTT 网页面板或苹果「家庭」App 远程控制，无需安装专用 App。项目当前固件版本为 **v1.5.31**。


## 主要功能

| 功能 | 当前实现 |
| --- | --- |
| WiFi 配网 | 设备热点 + Captive Portal，支持扫描附近 2.4 GHz WiFi |
| MQTT 控制 | 网页远程开关、调温、模式、风速和扩展功能，状态每 10 秒上报 |
| HomeKit | 支持苹果「家庭」App 和 Siri，提供恒温器、风速及模式控制 |
| 红外控制 | 基于 IRremoteESP8266，支持多种空调协议和协议自动识别 |
| 环境监测 | AHT20 实时采集温度、湿度，并同步至 MQTT 和 HomeKit |
| 本地定时 | 最多保存 10 个任务，支持一次执行、每日重复、启用和停用 |
| OTA 升级 | WiFi 主线支持在线检查、确认下载、进度上报和自动重启 |
| 状态指示 | WS2812B 以不同颜色显示配网、连接、学习、发射和重置状态 |
| 自动恢复 | WiFi 断线后按 5/10/20/30 秒退避重连，持续 30 秒未恢复时开启配网热点 |

## 硬件

### 硬件清单

- ESP32 开发板或项目 PCB（Flash 至少 4 MB）
- 红外发射管及驱动电路
- TSOP34838 或兼容的 38 kHz 红外接收头
- AHT20 温湿度传感器
- WS2812B RGB LED
- USB 供电与下载接口

### 当前 USB 版引脚

| 功能 | ESP32 引脚 | 说明 |
| --- | --- | --- |
| 红外发射 | GPIO4 | 当前 PCB 使用的发射脚 |
| 红外接收 | GPIO23 | 用于识别空调遥控器协议 |
| AHT20 SDA | GPIO21 | I2C 数据线 |
| AHT20 SCL | GPIO22 | I2C 时钟线 |
| WS2812B | GPIO2 | 单颗 RGB 状态灯 |
| BOOT / 功能键 | GPIO0 | 启动配置脚，按键操作需谨慎 |

引脚统一定义在 [`src/PinConfig.h`](src/PinConfig.h)。如果使用其他 PCB，请先按实际接线修改该文件。

## 快速开始（推荐 WiFi 版）

### 1. 编译并烧录

安装 [Visual Studio Code](https://code.visualstudio.com/) 和 [PlatformIO](https://platformio.org/) 后，在项目根目录执行：

```powershell
pio run -e esp32_wifi -t upload
```

Windows 也可以使用项目自带脚本：

```powershell
.\build_wifi.bat
.\upload_wifi_com17.bat
```

第二个脚本固定使用 `COM17`。如果开发板端口不同，请使用 PlatformIO 命令并将端口替换为实际值：

```powershell
pio run -e esp32_wifi -t upload --upload-port COMx
```

串口监视器波特率为 `115200`。

### 2. 配置 WiFi

首次启动时，设备没有保存的网络信息，会自动进入配网模式：

1. 手机连接开放热点 `ESP32AC_<12位设备号>`。
2. 等待系统自动弹出配网页；未弹出时访问 `http://192.168.4.1`。
3. 扫描并选择附近的 **2.4 GHz WiFi**，输入密码后保存。
4. 连接成功后，绿色指示灯熄灭，设备热点也会自动关闭。

配网页会同时显示设备专属 MQTT 主题，例如：

```text
ac/esp32/a1b2c3d4e5f6
```

请保存这个主题，网页控制时需要使用。设备只支持 2.4 GHz WiFi，不支持仅 5 GHz 的网络。

### 3. 使用 MQTT 网页控制

打开在线面板：[ESP32/ESP8266 空调 MQTT 控制面板](https://zhanghaompc.github.io/mqtt-control/)。

在「连接配置」中选择 ESP32，并填写配网页显示的完整主题前缀。当前默认配置为：

| 配置项 | 默认值 |
| --- | --- |
| MQTT Broker | `broker-cn.emqx.io` |
| 设备 TCP 端口 | `1883` |
| 网页面板 WSS 端口 | `8084` |
| WebSocket 路径 | `/mqtt` |
| 加密 | 开启 |
| 用户名 / 密码 | 留空 |
| 主题前缀 | `ac/esp32/<12位设备号>` |

网页和设备必须使用同一个 Broker 与主题前缀。连接 Broker 成功只代表网页已联网；页面同时显示“设备在线”后，才表示 ESP32 已通过 MQTT 接入。

> 默认使用公共 MQTT Broker，适合开发与演示，不适合传输敏感信息或直接用于正式环境。正式部署建议改用带账号、访问控制和 TLS 的私有 Broker。当前固件的 MQTT 参数保存在 SPIFFS 的 `/mqtt.json`，项目尚未在配网页提供修改入口。

### 4. 添加 HomeKit（可选）

WiFi 连接成功后，固件会启动 HomeKit 服务：

1. 打开 iPhone 或 iPad 的「家庭」App。
2. 选择“添加配件”并添加附近设备。
3. 输入配对码 `11122333`。
4. 添加完成后即可在「家庭」App 或通过 Siri 控制。

该配对码写在固件中，准备公开部署时请更换为自己的配对信息。

## 固件环境

项目在 [`platformio.ini`](platformio.ini) 中保留当前发布环境：

| 环境 | 启动模式 | 功能 | 适用场景 |
| --- | --- | --- | --- |
| `esp32_wifi` | WiFi | MQTT、HomeKit、网页配网、双分区 OTA | **当前唯一发布环境** |

对应的编译命令：

```powershell
pio run -e esp32_wifi
```

## MQTT 通信约定

设备使用基于完整 eFuse MAC 生成的唯一主题前缀：

```text
ac/esp32/<12位小写十六进制设备号>
```

在此前缀后追加不同后缀：

| 主题 | 方向 | 用途 |
| --- | --- | --- |
| `<主题前缀>/in` | 面板 → 设备 | 下发文本控制指令 |
| `<主题前缀>/out` | 设备 → 面板 | 返回指令执行结果与 OTA 进度 |
| `<主题前缀>/status` | 设备 → 面板 | 每 10 秒发布一次 retained JSON 状态 |

设备上线时发布完整状态，意外离线时通过 MQTT 遗嘱在 `/status` 发布 `{"online":false}`。典型在线状态如下：

```json
{
  "temp": 26.5,
  "humidity": 55.2,
  "power": true,
  "mode": 1,
  "speed": 2,
  "degrees": 24,
  "protocol": "KELVINATOR",
  "online": true,
  "turbo": false,
  "swing": false,
  "light": true,
  "sleep": false,
  "clean": false,
  "fw": "1.5.31"
}
```

MQTT 使用以下文本命令控制设备，例如：

```text
temp=24;mode=1;speed=2;power=on
power=off
turbo
swing
light
sleep
clean
protocol=KELVINATOR
get_protocol
learn=start
timer=list
ota=check
```

## 协议学习与定时任务

### 协议学习

在网页面板中开始学习后，设备会等待 10 秒。将原装空调遥控器对准红外接收头并按下按键；识别成功后协议会保存到 SPIFFS，下次开机继续使用。未识别时可在界面中手动选择协议。

常见品牌可优先尝试：

| 品牌 | 建议协议 |
| --- | --- |
| 格力 | `KELVINATOR` 或 `GREE` |
| 美的 | `COOLIX` 或 `MIDEA` |
| 海尔 | `HAIER_AC` 或 `HAIER_AC_YRW02` |
| 大金 | `DAIKIN` 系列 |
| 三菱 | `MITSUBISHI_AC` 或 `MITSUBISHI_HEAVY_*` |

协议名称代表红外编码族，同一品牌的不同遥控器型号可能使用不同协议，应以实机测试为准。

### 定时任务

- 最多保存 10 个任务。
- 支持新增、原地修改、删除、启用和停用。
- 一次性任务执行后会自动停用；重复任务每天执行。
- 任务存储在设备 SPIFFS 中，掉电不会丢失。
- 设备联网后通过阿里云 NTP 按东八区校时。
- ESP32 没有后备电池 RTC，长时间完全断电后需重新联网或由手机校时。

## 状态灯与按键

| 设备状态 | 灯效 |
| --- | --- |
| WiFi 等待连接 / 配网 | 绿色闪烁 |
| WiFi 已连接 | 熄灭 |
| 红外协议学习 | 紫色 |
| 红外发射 | 红色 |
| 按键已按住 1.5 秒 | 黄色常亮 |
| 恢复出厂或 OTA 下载 | 白色闪烁 |

运行中长按 BOOT 键 3 秒会恢复出厂设置并重启，清除 HomeKit 配对、WiFi 凭据、已保存协议和定时任务。GPIO0 同时是 ESP32 启动模式脚：上电或复位时一直按住会进入下载模式，这属于芯片正常行为。

更多说明见 [`LED状态说明.md`](LED状态说明.md)。

## OTA 升级

推荐的 `esp32_wifi` 环境使用 `huge_app_ota.csv` 双应用分区。网页面板发送检查命令后，设备会读取与构建环境对应的版本清单；发现新版本时先等待用户确认，再异步下载并上报进度，写入完成后自动重启。

发布工具会更新固件版本、编译产物和 OTA 清单：

```powershell
.\release.ps1 -Environment esp32_wifi -Version 1.5.32
```

发布脚本会提交并推送代码，使用前请先确认工作区内容和目标版本。详细流程见 [`OTA流程说明.md`](OTA流程说明.md)。

## 项目结构

```text
├── src/                       ESP32 固件源码
│   ├── main.cpp               初始化、运行模式与 HomeKit 服务
│   ├── WifiManagerEx.*        WiFi 配网、重连和 Captive Portal
│   ├── MqttManager.*          MQTT 连接、指令和状态上报
│   ├── IrManager.*            独立 FreeRTOS 红外发射任务
│   ├── SensorManager.*        AHT20 温湿度采集
│   ├── TimerManager.*         本地定时任务
│   ├── OtaManager.*           在线升级
│   ├── LedManager.*           RGB 状态灯
│   ├── PinConfig.h            硬件引脚定义
│   └── DeviceConfig.h         固件版本与设备标识
├── MQTT_CONTROL/              MQTT 网页控制面板
├── firmware/                  已发布固件与 OTA 清单
├── docs/images/               项目图片
├── platformio.ini             三种固件环境配置
└── release.ps1                Windows 发布脚本
```

## 技术实现

- Arduino Framework + PlatformIO
- [IRremoteESP8266 2.8.6](https://github.com/crankyoldgit/IRremoteESP8266)
- [HomeSpan 1.9.1](https://github.com/HomeSpan/HomeSpan)
- [PubSubClient 2.8](https://github.com/knolleary/pubsubclient)
- Adafruit AHTX0、FastLED、ArduinoJson、NTPClient
- 红外发射运行在独立 FreeRTOS 任务中，降低 WiFi/MQTT 活动对红外时序的影响
- WiFi 扫描采用异步流程，避免配网页在扫描期间失去响应
- 配置、协议和定时任务持久化到 SPIFFS

## 故障排查

### 搜不到配网热点

- 确认烧录的是 `esp32_wifi`。
- 首次启动会立即出现热点；已有 WiFi 凭据时，设备先自动重连，持续约 30 秒失败后才重新开启热点。
- 热点全名包含 12 位设备号，不是固定的 `ESP32AC_xxxx`。

### 网页显示 Broker 已连接，但设备离线

- 确认网页主题与配网页显示的主题完全一致，包含全部 12 位设备号。
- 确认设备与网页使用同一个 Broker。
- 公共 Broker 偶尔会限流或短暂不可用，可稍后重试。

### 空调没有响应

- 确认使用的是当前 PCB 的 GPIO4 发射接线。
- 让红外发射管朝向空调接收窗，并检查驱动电路和供电。
- 重新执行协议学习，或尝试同品牌的其他协议族。
- 部分协议并不支持强劲、灯光、自清洁等全部扩展功能。

### WiFi 无法连接

- 只使用 2.4 GHz 网络。
- 检查密码、隐藏 SSID 和路由器兼容性。
- 必要时运行中长按 BOOT 3 秒清除 WiFi 凭据后重新配网。

开发过程中的问题记录见 [`项目问题总结.md`](项目问题总结.md)，PlatformIO 使用提示见 [`PLATFORMIO_TIPS.md`](PLATFORMIO_TIPS.md)。

## 效果展示

| 设备与 PCB | 外壳与结构 |
| --- | --- |
| ![设备实物](docs/images/device.jpg) | ![外壳](docs/images/case.jpg) |
| ![PCB](docs/images/pcb.png) | ![3D 盒体](docs/images/box-3d.png) |

**HomeKit 家庭 App**

![HomeKit 连接](docs/images/homekit.jpg)

## License

本项目仅供学习、交流和个人非商业用途。使用红外发射、公共 MQTT 服务及市电相关外设时，请自行评估硬件与网络安全风险。
