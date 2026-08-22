<template>
  <view class="container">
    <view class="section-title">状态灯说明</view>
    <view class="card">
      <view class="row">
        <view class="led blue blink"></view>
        <view class="row-text">
          <text class="row-name">BLE 等待连接</text>
          <text class="row-desc">蓝色 · 闪烁</text>
        </view>
      </view>
      <view class="row">
        <view class="led cyan"></view>
        <view class="row-text">
          <text class="row-name">BLE 已连接</text>
          <text class="row-desc">青色 · 常亮</text>
        </view>
      </view>
      <view class="row">
        <view class="led green blink"></view>
        <view class="row-text">
          <text class="row-name">WiFi 等待连接</text>
          <text class="row-desc">绿色 · 闪烁</text>
        </view>
      </view>
      <view class="row">
        <view class="led off"></view>
        <view class="row-text">
          <text class="row-name">WiFi 已连接</text>
          <text class="row-desc">熄灭</text>
        </view>
      </view>
      <view class="row">
        <view class="led purple blink"></view>
        <view class="row-text">
          <text class="row-name">协议学习</text>
          <text class="row-desc">紫色 · 闪烁</text>
        </view>
      </view>
      <view class="row">
        <view class="led red"></view>
        <view class="row-text">
          <text class="row-name">红外发射</text>
          <text class="row-desc">红色 · 常亮</text>
        </view>
      </view>
      <view class="row">
        <view class="led yellow"></view>
        <view class="row-text">
          <text class="row-name">长按重置待触发</text>
          <text class="row-desc">黄色 · 常亮</text>
        </view>
      </view>
      <view class="row">
        <view class="led white blink"></view>
        <view class="row-text">
          <text class="row-name">恢复出厂设置</text>
          <text class="row-desc">白色 · 闪烁</text>
        </view>
      </view>
    </view>

    <view class="section-title">常见问题与解决办法</view>

    <view class="card">
      <view class="faq" v-for="(item, index) in faqs" :key="index">
        <view class="faq-q">Q{{ index + 1 }}：{{ item.q }}</view>
        <view class="faq-a">{{ item.a }}</view>
      </view>
    </view>
  </view>
</template>

<script>
export default {
  data() {
    return {
      faqs: [
        {
          q: '手机搜不到设备？',
          a: '确认设备已上电、靠近手机（10 米内）；检查手机蓝牙已开启并已授权定位/蓝牙权限；确认设备未被其他手机占用；必要时重启设备后重新扫描。'
        },
        {
          q: '红外发射了但空调没反应？',
          a: '确认红外发射头对准空调接收窗口、距离 3~5 米内、中间无遮挡；确认空调协议正确（可在“协议学习”中对准遥控器按键识别）；避免阳光或强光直射接收头。'
        },
        {
          q: '风速或模式调节没生效？',
          a: '旧固件存在风速解析问题，请升级到最新固件（IRremoteESP8266 2.8.6 版本已修复），升级后风速可正常调节。'
        },
        {
          q: '定时任务到点没执行？',
          a: '定时任务由设备本地执行，需要设备时间正确。连接 App 会自动校时；设备断电后请重新连接一次 App 完成校时。'
        },
        {
          q: '编辑定时任务提示“操作超时”？',
          a: '说明设备固件版本较旧，不支持 timer=update 原地更新命令。请重新烧录最新固件后再试。'
        },
        {
          q: 'HomeKit 配对不上？',
          a: '确认设备 WiFi 已连接（状态灯熄灭表示已连接）；使用配对码 11122333 配对；仍失败可先恢复出厂设置（长按 BOOT 键 3 秒）再重新配对。'
        },
        {
          q: '怎么恢复出厂设置？',
          a: '两种方式：App 首页底部“恢复出厂设置”，或按住设备 BOOT 键 3 秒（黄灯 → 白灯闪烁 → 自动重启）。会清除 HomeKit 配对、WiFi 配置、定时任务和空调协议。'
        },
        {
          q: 'WiFi 连不上怎么办？',
          a: '未保存 WiFi 凭据时设备会弹出“WIFI配置”门户配网；也可以在已连接时短按 BOOT 键在 BLE/WiFi 模式间切换，或通过串口使用 HomeSpan 命令模式配置。'
        }
      ]
    }
  }
}
</script>

<style>
.container {
  padding: 28rpx 24rpx calc(60rpx + env(safe-area-inset-bottom));
}

.section-title {
  font-size: 30rpx;
  font-weight: 700;
  color: var(--c-text, #172033);
  margin: 8rpx 0 18rpx;
}

.card {
  background: var(--c-surface, #ffffff);
  border: 1rpx solid var(--c-border, #e6edf7);
  border-radius: var(--r-lg, 32rpx);
  box-shadow: var(--shadow-card, 0 12rpx 32rpx rgba(37, 99, 235, 0.08));
  padding: 8rpx 28rpx;
  margin-bottom: 28rpx;
}

.row {
  display: flex;
  align-items: center;
  gap: 20rpx;
  padding: 20rpx 0;
  border-bottom: 1rpx solid var(--c-border, #e6edf7);
}

.row:last-child {
  border-bottom: none;
}

.led {
  width: 36rpx;
  height: 36rpx;
  border-radius: 50%;
  flex-shrink: 0;
}

.led.blue {
  background: #3b82f6;
}

.led.cyan {
  background: #06b6d4;
}

.led.green {
  background: #22c55e;
}

.led.purple {
  background: #a855f7;
}

.led.red {
  background: #ef4444;
}

.led.yellow {
  background: #f59e0b;
}

.led.white {
  background: #e5e7eb;
  border: 1rpx solid #cbd5e1;
}

.led.off {
  background: #e2e8f0;
}

.led.blink {
  animation: led-blink 1.2s ease-in-out infinite;
}

.row-text {
  display: flex;
  flex-direction: column;
}

.row-name {
  font-size: 28rpx;
  font-weight: 600;
  color: var(--c-text, #172033);
}

.row-desc {
  margin-top: 4rpx;
  font-size: 22rpx;
  color: var(--c-muted, #64748b);
}

.faq {
  padding: 22rpx 0;
  border-bottom: 1rpx solid var(--c-border, #e6edf7);
}

.faq:last-child {
  border-bottom: none;
}

.faq-q {
  font-size: 27rpx;
  font-weight: 600;
  color: var(--c-text, #172033);
}

.faq-a {
  margin-top: 10rpx;
  font-size: 24rpx;
  color: var(--c-muted, #64748b);
  line-height: 1.6;
}

@keyframes led-blink {
  0%,
  100% {
    opacity: 1;
  }
  50% {
    opacity: 0.3;
  }
}

@media (prefers-reduced-motion: reduce) {
  .led.blink {
    animation: none;
  }
}
</style>
