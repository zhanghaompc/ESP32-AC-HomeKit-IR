<template>
  <view class="conn-panel">
    <view v-if="error && state !== 'connected'" class="error-banner">
      <text class="error-text">{{ error }}</text>
      <view class="error-actions">
        <text class="error-link" @tap="$emit('retry')">重试</text>
        <text class="error-link" @tap="$emit('open-settings')">去设置</text>
      </view>
    </view>

    <view class="conn-card">
      <view class="icon-ring" :class="state">
        <text v-if="state === 'connected'" class="icon-ok">✓</text>
        <text v-else-if="state === 'connecting' || state === 'reconnecting'" class="icon-spin">↻</text>
        <text v-else class="icon-bt">⌁</text>
      </view>

      <view class="card-title">{{ title }}</view>
      <view class="card-hint">{{ hint }}</view>

      <button
        v-if="state === 'idle'"
        class="m-btn primary block"
        @tap="$emit('scan')"
      >
        连接设备
      </button>
      <button
        v-else-if="state === 'scanning'"
        class="m-btn secondary block"
        @tap="$emit('stop-scan')"
      >
        停止扫描
      </button>
      <button
        v-else-if="state === 'connecting'"
        class="m-btn secondary block"
        @tap="cancelConnecting"
      >
        取消
      </button>
      <button
        v-else-if="state === 'reconnecting'"
        class="m-btn secondary block"
        @tap="$emit('stop-reconnect')"
      >
        取消重连
      </button>
      <button
        v-else-if="state === 'connected'"
        class="m-btn danger block"
        @tap="$emit('disconnect')"
      >
        断开连接
      </button>
    </view>

    <view v-if="state === 'idle' && savedDevice" class="recent-card">
      <view class="recent-icon">↻</view>
      <view class="recent-info">
        <view class="recent-name">{{ savedDevice.name || savedDevice.deviceId }}</view>
        <view class="recent-sub">上次连接过的设备</view>
      </view>
      <view class="recent-actions">
        <text class="linklike strong" @tap="$emit('connect-saved', savedDevice.deviceId)">连接</text>
        <text class="linklike muted" @tap="$emit('forget-device')">忘记</text>
      </view>
    </view>

    <view v-if="state === 'scanning'" class="device-section">
      <view class="list-head">
        <text class="list-title">{{ deviceList.length > 0 ? `发现 ${deviceList.length} 台设备` : '正在搜索设备…' }}</text>
        <text class="linklike" @tap="$emit('scan')">重新扫描</text>
      </view>

      <view v-if="deviceList.length === 0" class="empty-devices">
        <text class="empty-icon">📡</text>
        <text class="empty-text">还没发现设备</text>
        <text class="empty-sub">请确认 ESP32 已上电并处于可发现状态</text>
      </view>

      <view
        v-for="device in deviceList"
        :key="device.deviceId"
        class="device-row"
        @tap="$emit('connect-device', device.deviceId)"
      >
        <view class="dev-icon">⌁</view>
        <view class="dev-info">
          <view class="dev-name">
            <text>{{ device.name || device.deviceId }}</text>
            <text v-if="device.deviceId === savedDeviceId" class="tag">已连接过</text>
          </view>
          <view class="dev-sub">
            <view class="bars">
              <view
                v-for="n in 4"
                :key="n"
                class="bar"
                :class="{ on: n <= rssiLevel(device.RSSI).bars }"
              ></view>
            </view>
            <text>{{ rssiLevel(device.RSSI).label }} · {{ device.RSSI || '--' }} dBm</text>
          </view>
        </view>
        <text class="row-arrow">›</text>
      </view>

      <view class="help-toggle" @tap="showHelp = !showHelp">
        <text class="linklike">{{ showHelp ? '收起排查步骤' : '搜不到设备？查看排查步骤' }}</text>
      </view>

      <view v-if="showHelp" class="help-list">
        <view class="help-item">1. 确认 ESP32 空调控制器已上电</view>
        <view class="help-item">2. 手机与设备保持在 10 米以内</view>
        <view class="help-item">3. 确认手机蓝牙已开启</view>
        <view class="help-item">4. 允许本 App 的蓝牙和定位权限</view>
        <view class="help-item">5. 确认设备未被其他手机连接占用</view>
      </view>
    </view>
  </view>
</template>

<script>
export default {
  name: 'ConnectPanel',
  props: {
    state: {
      type: String,
      default: 'idle'
    },
    deviceList: {
      type: Array,
      default: () => []
    },
    savedDevice: {
      type: Object,
      default: null
    },
    savedDeviceId: {
      type: String,
      default: ''
    },
    error: {
      type: String,
      default: ''
    }
  },
  emits: [
    'scan',
    'stop-scan',
    'connect-device',
    'connect-saved',
    'disconnect',
    'stop-reconnect',
    'forget-device',
    'open-settings',
    'retry'
  ],
  data() {
    return {
      showHelp: false
    }
  },
  computed: {
    title() {
      switch (this.state) {
        case 'scanning':
          return '正在搜索附近的空调设备'
        case 'connecting':
          return '正在连接设备'
        case 'connected':
          return '设备已连接'
        case 'reconnecting':
          return '连接已断开，正在重连'
        default:
          return '未连接设备'
      }
    },
    hint() {
      switch (this.state) {
        case 'scanning':
          return '请确认 ESP32 已上电并处于可发现状态'
        case 'connecting':
          return '请稍候，正在建立蓝牙连接…'
        case 'connected':
          return '可以开始控制空调了'
        case 'reconnecting':
          return '请保持设备在附近，稍后会自动恢复'
        default:
          return '打开设备电源并靠近手机，然后开始搜索'
      }
    }
  },
  methods: {
    rssiLevel(rssi) {
      if (typeof rssi !== 'number' || isNaN(rssi)) {
        return { bars: 0, label: '未知' }
      }
      if (rssi >= -55) return { bars: 4, label: '强' }
      if (rssi >= -70) return { bars: 3, label: '中' }
      if (rssi >= -80) return { bars: 2, label: '弱' }
      return { bars: 1, label: '很弱' }
    },
    cancelConnecting() {
      this.$emit('stop-reconnect')
    }
  }
}
</script>

<style scoped>
.conn-panel {
  margin-bottom: 24rpx;
}

.error-banner {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12rpx;
  padding: 16rpx 20rpx;
  border-radius: 16rpx;
  background: #fef2f2;
  margin-bottom: 16rpx;
}

.error-text {
  flex: 1;
  font-size: 24rpx;
  color: #b91c1c;
  line-height: 1.4;
}

.error-actions {
  display: flex;
  gap: 18rpx;
  flex-shrink: 0;
}

.error-link {
  font-size: 24rpx;
  color: #dc2626;
  font-weight: 600;
}

.conn-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 40rpx 24rpx 34rpx;
  background: #ffffff;
  border-radius: 26rpx;
  box-shadow: 0 14rpx 36rpx rgba(37, 99, 235, 0.10);
  border: 1rpx solid rgba(255, 255, 255, 0.8);
}

.icon-ring {
  width: 104rpx;
  height: 104rpx;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 52rpx;
  margin-bottom: 20rpx;
}

.icon-ring.idle {
  background: linear-gradient(135deg, #dbeafe, #bfdbfe);
  color: #3a7bd5;
  box-shadow: 0 10rpx 24rpx rgba(58, 123, 213, 0.22);
}

.icon-ring.scanning {
  background: linear-gradient(135deg, #fef3c7, #fde68a);
  color: #d97706;
  box-shadow: 0 10rpx 24rpx rgba(217, 119, 6, 0.20);
}

.icon-ring.connecting,
.icon-ring.reconnecting {
  background: linear-gradient(135deg, #fef3c7, #fde68a);
  color: #d97706;
  box-shadow: 0 10rpx 24rpx rgba(217, 119, 6, 0.20);
}

.icon-ring.connected {
  background: linear-gradient(135deg, #d1fae5, #a7f3d0);
  color: #16a34a;
  box-shadow: 0 10rpx 24rpx rgba(22, 163, 74, 0.22);
}

.icon-spin {
  display: inline-block;
  animation: conn-spin 1.2s linear infinite;
}

.icon-ok {
  font-size: 56rpx;
  font-weight: 700;
}

.icon-bt {
  transform: rotate(-20deg);
}

.card-title {
  font-size: 34rpx;
  font-weight: 700;
  color: #111827;
  text-align: center;
}

.card-hint {
  margin-top: 10rpx;
  font-size: 24rpx;
  color: #6b7280;
  text-align: center;
  line-height: 1.5;
}

.m-btn {
  width: 100%;
  margin-top: 28rpx;
  border-radius: 20rpx;
  font-size: 30rpx;
  font-weight: 600;
  line-height: 2.5;
}

.m-btn.primary {
  color: #ffffff;
  background: linear-gradient(135deg, #6ba6f5, #3a7bd5);
  box-shadow: 0 12rpx 28rpx rgba(58, 123, 213, 0.28);
}

.m-btn.secondary {
  color: #374151;
  background: #f1f5f9;
}

.m-btn.danger {
  color: #dc2626;
  background: #fef2f2;
}

.recent-card {
  display: flex;
  align-items: center;
  gap: 16rpx;
  margin-top: 18rpx;
  padding: 20rpx 22rpx;
  background: #ffffff;
  border-radius: 20rpx;
  box-shadow: 0 12rpx 30rpx rgba(37, 99, 235, 0.08);
  border: 1rpx solid rgba(255, 255, 255, 0.8);
}

.recent-icon {
  width: 64rpx;
  height: 64rpx;
  border-radius: 18rpx;
  background: linear-gradient(135deg, #dbeafe, #bfdbfe);
  color: #3a7bd5;
  font-size: 32rpx;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.recent-info {
  flex: 1;
  min-width: 0;
}

.recent-name {
  font-size: 28rpx;
  font-weight: 600;
  color: #111827;
}

.recent-sub {
  font-size: 22rpx;
  color: #9ca3af;
  margin-top: 2rpx;
}

.recent-actions {
  display: flex;
  gap: 22rpx;
  flex-shrink: 0;
}

.linklike {
  font-size: 24rpx;
  color: #3a7bd5;
}

.linklike.strong {
  font-weight: 600;
}

.linklike.muted {
  color: #9ca3af;
}

.device-section {
  margin-top: 20rpx;
}

.list-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 4rpx 6rpx 14rpx;
}

.list-title {
  font-size: 26rpx;
  font-weight: 600;
  color: #374151;
}

.device-row {
  display: flex;
  align-items: center;
  gap: 16rpx;
  padding: 20rpx 22rpx;
  margin-bottom: 14rpx;
  background: #ffffff;
  border-radius: 20rpx;
  box-shadow: 0 12rpx 30rpx rgba(37, 99, 235, 0.08);
  border: 1rpx solid rgba(255, 255, 255, 0.8);
}

.dev-icon {
  width: 64rpx;
  height: 64rpx;
  border-radius: 18rpx;
  background: linear-gradient(135deg, #dbeafe, #bfdbfe);
  color: #3a7bd5;
  font-size: 32rpx;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.dev-info {
  flex: 1;
  min-width: 0;
}

.dev-name {
  display: flex;
  align-items: center;
  gap: 10rpx;
  font-size: 28rpx;
  font-weight: 600;
  color: #111827;
}

.tag {
  font-size: 20rpx;
  color: #16a34a;
  background: #e9f9ef;
  padding: 4rpx 12rpx;
  border-radius: 999rpx;
}

.dev-sub {
  display: flex;
  align-items: center;
  gap: 10rpx;
  margin-top: 8rpx;
  font-size: 22rpx;
  color: #6b7280;
}

.bars {
  display: inline-flex;
  align-items: flex-end;
  gap: 4rpx;
  height: 20rpx;
}

.bar {
  width: 6rpx;
  border-radius: 3rpx;
  background: #e5e7eb;
}

.bar:nth-child(1) {
  height: 8rpx;
}

.bar:nth-child(2) {
  height: 13rpx;
}

.bar:nth-child(3) {
  height: 17rpx;
}

.bar:nth-child(4) {
  height: 20rpx;
}

.bar.on {
  background: #16a34a;
}

.row-arrow {
  font-size: 36rpx;
  color: #cbd5e1;
  flex-shrink: 0;
}

.empty-devices {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8rpx;
  padding: 44rpx 0;
  background: #ffffff;
  border-radius: 20rpx;
}

.empty-icon {
  font-size: 56rpx;
}

.empty-text {
  font-size: 28rpx;
  color: #374151;
}

.empty-sub {
  font-size: 22rpx;
  color: #9ca3af;
}

.help-toggle {
  padding: 18rpx 0 4rpx;
  text-align: center;
}

.help-list {
  padding: 20rpx 24rpx;
  background: #f8fafc;
  border-radius: 18rpx;
  margin-top: 8rpx;
}

.help-item {
  font-size: 24rpx;
  color: #6b7280;
  line-height: 2;
}

@keyframes conn-spin {
  to {
    transform: rotate(360deg);
  }
}
</style>
