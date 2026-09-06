# 时欢智能空调助手（uni-app 双端版）

这是从微信小程序迁移过来的 Android / iOS 双端 App，使用 uni-app + Vue 3。
核心 BLE 连接和空调控制逻辑已经移植完成。

## 目录结构

```
app/
├── App.vue
├── main.js
├── manifest.json          # 应用配置、安卓/iOS 权限
├── pages.json             # 页面路由和导航栏配置
├── components/
│   └── connect-panel/
│       └── connect-panel.vue # 连接面板组件（状态机 + 设备列表 + 排查引导）
├── pages/
│   ├── index/index.vue    # 首页：BLE 连接 + 空调控制
│   └── timer/timer.vue    # 定时任务管理
└── utils/
    ├── constants.js       # 协议列表、模式/风速、BLE UUID
    └── ble-utils.js       # BLE 工具函数
```

## 第一步：安装 HBuilderX

1. 到 DCloud 官网下载 HBuilderX：<https://www.dcloud.io/hbuilderx.html>
2. 建议下载「App 开发版」。

## 第二步：打开项目

1. 打开 HBuilderX。
2. 菜单 `文件 -> 打开目录`，选择本目录（`app/`）。
3. 首次打开后，HBuilderX 会自动识别 uni-app 项目。

## 第三步：获取 AppID（必须）

`manifest.json` 里的 `appid` 目前为空，打包前需要补上：

1. 用 DCloud 账号登录 HBuilderX。
2. 双击打开 `manifest.json`，点击「基础配置」里的「重新获取 AppID」。
3. 获取后会自动写入 `appid`。

## 第四步：安卓运行 / 打包

### 真机调试

1. 手机开启「开发者选项」和「USB 调试」。
2. 用数据线连接电脑，手机上允许 USB 调试。
3. HBuilderX 菜单 `运行 -> 运行到手机或模拟器 -> 运行到 Android App 基座`。

### 打正式 APK

1. HBuilderX 菜单 `发行 -> 原生App-云打包`。
2. 平台选择 `Android`，证书按需选择（测试可先勾选「使用公共测试证书」）。
3. 打包完成后下载 APK，安装到手机即可。

## 第五步：iOS 运行 / 打包

iOS 需要 Apple 开发者证书。Windows 上不能本地编译 iOS，请用下面任意一种方式：

- HBuilderX 菜单 `发行 -> 原生App-云打包`，平台选 `iOS`（需要配置 iOS 证书和描述文件）。
- 或者在 Mac 上用 HBuilderX 本地运行/打包。

## 首次使用 App 时

- 确认 ESP32 空调控制器已上电并处于可被搜索状态。
- 手机弹出蓝牙、定位权限时选择「允许」。
- 首页连接面板会经历 未连接 → 扫描中 → 已连接 三个状态：点击「连接设备」搜索，
  在设备列表中选择目标设备连接；断线后会自动重连（最多 3 次）。
- 连接成功后即可控制空调、学习协议、管理定时任务。

## 注意事项

- 定时任务页顶部时间直接使用手机本地时间显示；「同步时间到设备」会把手机时间写入设备 RTC
  （固件需支持 `time=YYYY-MM-DD HH:MM:SS` 命令），BLE 模式下设备也能准点执行定时任务。
- 定时任务列表依赖 ESP32 固件把超长回包分片发送（每片 ≤18 字节并以 `\n` 结尾），
  否则 BLE 单包 20 字节限制会导致 `timers=[...]` 等回包被截断、任务页无法解析。
  固件端需要给 `src/BleManager.cpp` 增加 `sendChunked()` 并替换响应发送逻辑。
- 原小程序里的「切换到 WiFi 模式」会调用 `wx.exitMiniProgram` 退出小程序；App 里没有退出概念，这里改为只发送 `wifi_mode` 命令并提示。
- BLE 服务 UUID 使用 Nordic UART：`6E400001/6E400002/6E400003`，与 `utils/constants.js` 保持一致即可。
