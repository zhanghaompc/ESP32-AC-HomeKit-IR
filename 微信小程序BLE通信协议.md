# 微信小程序 BLE 通信协议文档

## 概述

本文档描述微信小程序与ESP32空调控制器之间的BLE通信协议。

---

## BLE 服务信息

| 项目 | UUID |
|------|------|
| 服务 UUID | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| 写特征 UUID (RX) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| 通知特征 UUID (TX) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

---

## 通信流程

### 1. 连接流程

```
1. 扫描设备 (名称: ESP32-AC)
2. 连接设备
3. 获取服务
4. 启用通知特征
5. 接收初始数据:
   - temp=XX.X;humidity=XX.X  (温湿度)
   - protocol=XXX              (当前协议)
```

### 2. 数据接收

ESP32会主动推送以下数据：

| 数据类型 | 格式 | 说明 |
|----------|------|------|
| 温湿度 | `temp=25.0;humidity=50.0` | 定期推送，数据变化时发送 |
| 协议 | `protocol=KELVINATOR` | 连接时发送一次 |
| 状态 | `temp=25;mode=0;speed=0;power=on` | 控制命令的响应 |

---

## 命令列表

### 1. 查询状态

**发送:** `status`

**返回:** 
```
temp=25.0;humidity=50.0;power=on;protocol=KELVINATOR
```

**字段说明:**
| 字段 | 类型 | 说明 |
|------|------|------|
| temp | float | 当前温度 |
| humidity | float | 当前湿度 |
| power | string | 电源状态: `on` / `off` |
| protocol | string | 当前空调协议 |

---

### 2. 获取当前协议

**发送:** `get_protocol`

**返回:**
```
protocol=KELVINATOR
```

---

### 3. 设置协议

**发送:** `protocol=KELVINATOR`

**返回:**
```
protocol=KELVINATOR        // 设置成功
protocol=invalid           // 协议无效
```

**支持的协议列表:**
```
KELVINATOR, MITSUBISHI_AC, GREE, DAIKIN, TOSHIBA_AC, 
LG, SAMSUNG_AC, MIDEA, HAIER_AC, CHIGO, 
PANASONIC_AC, WHIRLPOOL_AC, SANYO_AC, SHARP_AC, FUJITSU_AC
```

---

### 4. 打开空调

**发送:**
```
temp=25;mode=0;speed=0;power=on
```

**参数说明:**
| 参数 | 类型 | 说明 |
|------|------|------|
| temp | int | 目标温度 (17-30) |
| mode | int | 模式: 0=自动, 1=制冷, 2=制热, 3=送风, 4=除湿 |
| speed | int | 风速: 0=自动, 1=低, 2=中, 3=高 |
| power | string | 固定为 `on` |

**返回:**
```
temp=25;mode=0;speed=0;power=on
```

---

### 5. 关闭空调

**发送:** `power=off`

**返回:** `power=off`

---

### 6. 红外学习模式

**发送:** `learn=start`

**返回:**
```
learn=success:KELVINATOR    // 学习成功，返回识别到的协议
learn=timeout               // 10秒超时未收到红外信号
```

**流程:**
1. 小程序发送 `learn=start`
2. ESP32返回 `learn=waiting`
3. 用户对着ESP32按空调遥控器
4. ESP32识别协议后返回 `learn=success:XXX`
5. 协议自动保存到ESP32，掉电不丢失

---

## 小程序代码示例

### 1. 初始化BLE连接

```javascript
// pages/index/index.js
const BLE_SERVICE_UUID = '6E400001-B5A3-F393-E0A9-E50E24DCCA9E'
const BLE_CHAR_RX_UUID = '6E400002-B5A3-F393-E0A9-E50E24DCCA9E'
const BLE_CHAR_TX_UUID = '6E400003-B5A3-F393-E0A9-E50E24DCCA9E'

Page({
  data: {
    deviceId: '',
    serviceId: BLE_SERVICE_UUID,
    charRxId: BLE_CHAR_RX_UUID,
    charTxId: BLE_CHAR_TX_UUID,
    temperature: '--',
    humidity: '--',
    protocol: '--',
    isPowerOn: false
  },

  // 初始化蓝牙
  initBLE() {
    wx.openBluetoothAdapter({
      success: () => {
        this.startScan()
      },
      fail: (err) => {
        console.error('蓝牙初始化失败', err)
      }
    })
  },

  // 扫描设备
  startScan() {
    wx.startBluetoothDevicesDiscovery({
      services: [BLE_SERVICE_UUID],
      success: () => {
        wx.onBluetoothDeviceFound((res) => {
          const device = res.devices[0]
          if (device.name === 'ESP32-AC') {
            this.connectDevice(device.deviceId)
          }
        })
      }
    })
  },

  // 连接设备
  connectDevice(deviceId) {
    wx.createBLEConnection({
      deviceId: deviceId,
      success: () => {
        this.setData({ deviceId })
        this.getBLEDeviceServices()
      }
    })
  },

  // 获取服务
  getBLEDeviceServices() {
    wx.getBLEDeviceServices({
      deviceId: this.data.deviceId,
      success: (res) => {
        this.getBLEDeviceCharacteristics()
      }
    })
  },

  // 获取特征值并启用通知
  getBLEDeviceCharacteristics() {
    wx.getBLEDeviceCharacteristics({
      deviceId: this.data.deviceId,
      serviceId: this.data.serviceId,
      success: (res) => {
        // 启用通知
        wx.notifyBLECharacteristicValueChange({
          deviceId: this.data.deviceId,
          serviceId: this.data.serviceId,
          characteristicId: this.data.charTxId,
          state: true,
          success: () => {
            console.log('通知已启用')
          }
        })
      }
    })

    // 监听数据
    wx.onBLECharacteristicValueChange((res) => {
      const data = this.ab2str(res.value)
      this.parseData(data)
    })
  },

  // 解析接收的数据
  parseData(data) {
    console.log('收到数据:', data)

    // 解析温湿度
    if (data.startsWith('temp=')) {
      const parts = data.split(';')
      parts.forEach(part => {
        if (part.startsWith('temp=')) {
          this.setData({ temperature: part.substring(5) })
        }
        if (part.startsWith('humidity=')) {
          this.setData({ humidity: part.substring(9) })
        }
        if (part.startsWith('power=')) {
          this.setData({ isPowerOn: part.substring(6) === 'on' })
        }
        if (part.startsWith('protocol=')) {
          this.setData({ protocol: part.substring(9) })
        }
      })
    }

    // 解析协议
    if (data.startsWith('protocol=')) {
      this.setData({ protocol: data.substring(9) })
    }
  },

  // ArrayBuffer转字符串
  ab2str(buf) {
    return String.fromCharCode.apply(null, new Uint8Array(buf))
  },

  // 字符串转ArrayBuffer
  str2ab(str) {
    const buf = new ArrayBuffer(str.length)
    const bufView = new Uint8Array(buf)
    for (let i = 0; i < str.length; i++) {
      bufView[i] = str.charCodeAt(i)
    }
    return buf
  },

  // 发送命令
  sendCommand(cmd) {
    wx.writeBLECharacteristicValue({
      deviceId: this.data.deviceId,
      serviceId: this.data.serviceId,
      characteristicId: this.data.charRxId,
      value: this.str2ab(cmd)
    })
  },

  // 打开空调
  powerOn() {
    const cmd = `temp=25;mode=0;speed=0;power=on`
    this.sendCommand(cmd)
    this.setData({ isPowerOn: true })
  },

  // 关闭空调
  powerOff() {
    this.sendCommand('power=off')
    this.setData({ isPowerOn: false })
  },

  // 设置温度
  setTemp(temp) {
    const cmd = `temp=${temp};mode=0;speed=0;power=on`
    this.sendCommand(cmd)
  },

  // 查询状态
  queryStatus() {
    this.sendCommand('status')
  },

  // 获取协议
  getProtocol() {
    this.sendCommand('get_protocol')
  },

  // 设置协议
  setProtocol(protocol) {
    this.sendCommand(`protocol=${protocol}`)
  },

  // 开始学习
  startLearn() {
    this.sendCommand('learn=start')
  }
})
```

### 2. WXML界面示例

```xml
<!-- pages/index/index.wxml -->
<view class="container">
  <view class="status-card">
    <text class="label">温度: {{temperature}}℃</text>
    <text class="label">湿度: {{humidity}}%</text>
    <text class="label">协议: {{protocol}}</text>
  </view>

  <view class="control-panel">
    <button bindtap="powerOn" type="primary">打开空调</button>
    <button bindtap="powerOff" type="warn">关闭空调</button>
    <button bindtap="queryStatus">查询状态</button>
    <button bindtap="startLearn">红外学习</button>
  </view>

  <view class="temp-control">
    <text>目标温度: </text>
    <slider min="17" max="30" bindchange="onTempChange" />
  </view>

  <picker bindchange="onProtocolChange" range="{{protocols}}">
    <button>选择协议: {{protocol}}</button>
  </picker>
</view>
```

---

## 数据格式速查表

| 命令 | 发送格式 | 返回格式 |
|------|----------|----------|
| 查询状态 | `status` | `temp=XX;humidity=XX;power=on/off;protocol=XXX` |
| 获取协议 | `get_protocol` | `protocol=XXX` |
| 设置协议 | `protocol=XXX` | `protocol=XXX` 或 `protocol=invalid` |
| 打开空调 | `temp=X;mode=X;speed=X;power=on` | `temp=X;mode=X;speed=X;power=on` |
| 关闭空调 | `power=off` | `power=off` |
| 红外学习 | `learn=start` | `learn=success:XXX` 或 `learn=timeout` |

---

## 模式和风速编码

### 模式 (mode)
| 值 | 说明 |
|----|------|
| 0 | 自动 |
| 1 | 制冷 |
| 2 | 制热 |
| 3 | 送风 |
| 4 | 除湿 |

### 风速 (speed)
| 值 | 说明 |
|----|------|
| 0 | 自动 |
| 1 | 低速 |
| 2 | 中速 |
| 3 | 高速 |

---

## 注意事项

1. **协议保存**: 设置的协议会自动保存到ESP32的SPIFFS，掉电不丢失
2. **连接时自动发送**: 连接成功后ESP32会自动发送温湿度和当前协议
3. **温湿度更新**: 温度变化≥0.2℃或湿度变化≥0.5%时自动推送
4. **超时处理**: 红外学习模式超时时间为10秒
5. **数据解析**: 接收的数据可能包含多个字段，用分号`;`分隔

---

*文档版本: 1.0*
*更新日期: 2026-04-04*
