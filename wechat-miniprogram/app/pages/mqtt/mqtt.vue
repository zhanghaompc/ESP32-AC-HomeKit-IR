<template>
  <view class="page">
    <!-- 连接配置 -->
    <view class="card">
      <view class="card-title">MQTT 连接</view>
      <view class="row">
        <text class="label">服务器</text>
        <input class="input" v-model="cfg.host" placeholder="broker-cn.emqx.io" />
      </view>
      <view class="row">
        <text class="label">端口</text>
        <input class="input" v-model.number="cfg.port" type="number" placeholder="8083" />
        <text class="label" style="margin-left: 20rpx">加密</text>
        <switch :checked="cfg.tls" @change="e => (cfg.tls = e.detail.value)" />
      </view>
      <view class="row">
        <text class="label">路径</text>
        <input class="input" v-model="cfg.path" placeholder="/mqtt" />
      </view>
      <view class="row">
        <text class="label">用户名</text>
        <input class="input" v-model="cfg.username" placeholder="留空=匿名" />
      </view>
      <view class="row">
        <text class="label">密码</text>
        <input class="input" v-model="cfg.password" password placeholder="留空=匿名" />
      </view>
      <view class="row">
        <text class="label">主题</text>
        <input class="input" v-model="cfg.topic" placeholder="ac/esp32ac" />
      </view>
      <view class="row">
        <view class="status-pill" :class="connected ? 'on' : (connecting ? 'busy' : 'off')">
          {{ connected ? '已连接' : (connecting ? '连接中…' : '未连接') }}
        </view>
        <button class="btn primary" @tap="connected ? disconnect() : connect()">
          {{ connected ? '断开' : '连接' }}
        </button>
      </view>
    </view>

    <!-- 空调控制 -->
    <view class="card">
      <view class="card-title">空调控制</view>
      <view class="row">
        <text class="label">电源</text>
        <switch :checked="powerOn" @change="togglePower" />
        <text class="state" :class="powerOn ? 'on' : 'off'">{{ powerOn ? '开' : '关' }}</text>
      </view>
      <view class="row">
        <text class="label">温度</text>
        <button class="step-btn" @tap="tempStep(-1)">−</button>
        <text class="temp">{{ temp }}°C</text>
        <button class="step-btn" @tap="tempStep(1)">+</button>
      </view>
      <view class="grid">
        <view
          v-for="(m, i) in modeList"
          :key="'m' + i"
          class="grid-item"
          :class="{ active: i === modeIndex }"
          @tap="setMode(i)"
        >{{ m }}</view>
      </view>
      <view class="grid">
        <view
          v-for="(s, i) in speedList"
          :key="'s' + i"
          class="grid-item"
          :class="{ active: i === speedIndex }"
          @tap="setSpeed(i)"
        >{{ s }}</view>
      </view>
      <view v-if="statusText" class="status-text">{{ statusText }}</view>
    </view>

    <!-- 通用收发 -->
    <view class="card">
      <view class="card-title">通用发布（任意设备）</view>
      <view class="row">
        <text class="label">主题</text>
        <input class="input" v-model="pubTopic" :placeholder="cfg.topic + '/in'" />
      </view>
      <view class="row">
        <text class="label">内容</text>
        <input class="input" v-model="pubPayload" placeholder="发送 JSON 或文本，如 on=1" />
      </view>
      <view class="quick-row">
        <button class="mini-btn" @tap="quickPlug('1')">插座开</button>
        <button class="mini-btn" @tap="quickPlug('0')">插座关</button>
        <button class="mini-btn primary" @tap="pubSend">发送</button>
      </view>
    </view>

    <!-- 消息日志 -->
    <view class="card">
      <view class="card-title">消息日志</view>
      <scroll-view class="log" scroll-y>
        <view v-for="(item, i) in log" :key="i" class="log-item" :class="{ incoming: item.incoming }">
          <text class="log-time">{{ item.time }}</text>
          <text class="log-text">{{ item.text }}</text>
        </view>
        <view v-if="log.length === 0" class="log-empty">暂无消息，连接后订阅 {{ cfg.topic }}/#</view>
      </scroll-view>
    </view>
  </view>
</template>

<script>
import '../../utils/mqtt-shim.js'
// #ifdef MP-WEIXIN
const mqtt = require('../../utils/mqtt.min.js')
// #endif
// #ifndef MP-WEIXIN
import '../../utils/mqtt.min.js'
const mqtt = globalThis.mqtt
// #endif
import { MODE_LIST, SPEED_LIST } from '../../utils/constants.js'

export default {
  data() {
    return {
      cfg: {
        host: 'broker-cn.emqx.io',
        port: 8084,
        path: '/mqtt',
        tls: true,
        username: '',
        password: '',
        topic: 'ac/esp32ac'
      },
      connected: false,
      connecting: false,
      client: null,
      log: [],
      powerOn: false,
      temp: 26,
      modeIndex: 1,
      speedIndex: 2,
      statusText: '',
      pubTopic: '',
      pubPayload: ''
    }
  },
  computed: {
    modeList() { return MODE_LIST },
    speedList() { return SPEED_LIST }
  },
  onLoad() {
    const saved = uni.getStorageSync('mqtt_cfg2')
    if (saved) this.cfg = Object.assign(this.cfg, saved)
    this.pubTopic = this.cfg.topic + '/in'
  },
  onUnload() {
    this.disconnect()
  },
  methods: {
    buildUrl() {
      // 微信小程序用 wx/wxs，App/H5 用 ws/wss
      // #ifdef MP-WEIXIN
      const proto = this.cfg.tls ? 'wxs' : 'wx'
      // #endif
      // #ifndef MP-WEIXIN
      const proto = this.cfg.tls ? 'wss' : 'ws'
      // #endif
      return `${proto}://${this.cfg.host}:${this.cfg.port}${this.cfg.path || '/mqtt'}`
    },
    connect() {
      if (this.connecting) return
      this.connecting = true
      uni.setStorageSync('mqtt_cfg2', this.cfg)
      this.pubTopic = this.cfg.topic + '/in'
      this.addLog('连接 ' + this.buildUrl())

      this.client = mqtt.connect(this.buildUrl(), {
        username: this.cfg.username || undefined,
        password: this.cfg.password || undefined,
        clientId: 'uniapp_mqtt_' + Math.random().toString(16).slice(2, 10),
        reconnectPeriod: 5000,
        connectTimeout: 10000
      })

      this.client.on('connect', () => {
        this.connected = true
        this.connecting = false
        this.addLog('已连接')
        this.client.subscribe(this.cfg.topic + '/#', { qos: 0 })
      })
      this.client.on('message', (topic, payload) => {
        const msg = payload.toString()
        this.addLog(topic + ' : ' + msg, true)
        this.parseStatus(msg)
      })
      this.client.on('close', () => {
        this.connected = false
        this.connecting = false
        this.addLog('连接已断开')
      })
      this.client.on('error', (e) => {
        this.connecting = false
        this.addLog('错误: ' + ((e && e.message) || e))
      })
    },
    disconnect() {
      if (this.client) {
        try { this.client.end(true) } catch (e) {}
        this.client = null
      }
      this.connected = false
      this.connecting = false
    },
    addLog(text, incoming) {
      const time = new Date().toTimeString().slice(0, 8)
      this.log.unshift({ text, time, incoming: !!incoming })
      if (this.log.length > 100) this.log.pop()
    },
    parseStatus(msg) {
      if (msg.indexOf('{') === 0) {
        try {
          const j = JSON.parse(msg)
          if (j.degrees !== undefined) this.temp = Math.round(j.degrees)
          if (j.temp !== undefined && j.degrees === undefined) this.temp = Math.round(j.temp)
          if (j.power !== undefined) this.powerOn = !!j.power
          if (j.mode >= 0) this.modeIndex = j.mode
          if (j.speed >= 0) this.speedIndex = j.speed
          if (j.pows) this.statusText = j.pows
        } catch (e) {}
      } else if (msg.indexOf('temp=') === 0) {
        const t = msg.match(/temp=([\d.]+)/)
        if (t) this.temp = parseInt(t[1])
        const m = msg.match(/mode=(\d+)/)
        if (m) this.modeIndex = parseInt(m[1])
        const s = msg.match(/speed=(\d+)/)
        if (s) this.speedIndex = parseInt(s[1])
        const p = msg.match(/power=(on|off)/)
        if (p) this.powerOn = p[1] === 'on'
      }
    },
    send(cmd) {
      if (!this.connected) {
        uni.showToast({ title: '请先连接', icon: 'none' })
        return
      }
      const topic = this.cfg.topic + '/in'
      this.client.publish(topic, cmd, { qos: 0 })
      this.addLog(topic + ' : ' + cmd)
    },
    togglePower() {
      if (this.powerOn) this.send('power=off')
      else this.send(`temp=${this.temp};mode=${this.modeIndex};speed=${this.speedIndex};power=on`)
    },
    tempStep(d) {
      this.temp = Math.min(30, Math.max(16, this.temp + d))
      this.send(`temp=${this.temp};mode=${this.modeIndex};speed=${this.speedIndex};power=on`)
    },
    setMode(i) {
      this.modeIndex = i
      this.send(`temp=${this.temp};mode=${i};speed=${this.speedIndex};power=on`)
    },
    setSpeed(i) {
      this.speedIndex = i
      this.send(`temp=${this.temp};mode=${this.modeIndex};speed=${i};power=on`)
    },
    pubSend() {
      if (!this.connected) {
        uni.showToast({ title: '请先连接', icon: 'none' })
        return
      }
      const topic = this.pubTopic || (this.cfg.topic + '/in')
      this.client.publish(topic, this.pubPayload, { qos: 0 })
      this.addLog(topic + ' : ' + this.pubPayload)
    },
    quickPlug(v) {
      this.pubPayload = '{"m":"on","v":"' + v + '"}'
      this.pubSend()
    }
  }
}
</script>

<style scoped>
.page {
  padding: 24rpx;
  background: #f5f6f8;
  min-height: 100vh;
}
.card {
  background: #fff;
  border-radius: 16rpx;
  padding: 24rpx;
  margin-bottom: 24rpx;
  box-shadow: 0 2rpx 8rpx rgba(0, 0, 0, 0.05);
}
.card-title {
  font-size: 30rpx;
  font-weight: 600;
  color: #333;
  margin-bottom: 16rpx;
}
.row {
  display: flex;
  align-items: center;
  margin-bottom: 18rpx;
}
.label {
  width: 130rpx;
  font-size: 26rpx;
  color: #666;
  flex-shrink: 0;
}
.input {
  flex: 1;
  height: 64rpx;
  background: #f5f6f8;
  border-radius: 10rpx;
  padding: 0 16rpx;
  font-size: 26rpx;
}
.status-pill {
  padding: 8rpx 20rpx;
  border-radius: 24rpx;
  font-size: 24rpx;
  color: #fff;
}
.status-pill.on { background: #07c160; }
.status-pill.busy { background: #ffb800; }
.status-pill.off { background: #999; }
.btn {
  margin-left: auto;
  font-size: 26rpx;
  border-radius: 10rpx;
  line-height: 2.4;
  padding: 0 28rpx;
}
.btn.primary { background: #3A7BD5; color: #fff; }
.state { margin-left: 16rpx; font-size: 26rpx; }
.state.on { color: #07c160; }
.state.off { color: #999; }
.temp {
  font-size: 40rpx;
  font-weight: 600;
  margin: 0 24rpx;
  min-width: 130rpx;
  text-align: center;
}
.step-btn {
  width: 72rpx;
  height: 64rpx;
  line-height: 64rpx;
  padding: 0;
  font-size: 32rpx;
  background: #f0f3f8;
  color: #3A7BD5;
  border-radius: 10rpx;
}
.grid {
  display: flex;
  flex-wrap: wrap;
  margin-bottom: 12rpx;
}
.grid-item {
  width: 20%;
  box-sizing: border-box;
  padding: 14rpx 0;
  text-align: center;
  font-size: 24rpx;
  color: #555;
  background: #f5f6f8;
  border-radius: 10rpx;
  margin: 0 2% 12rpx 0;
}
.grid-item:last-child { margin-right: 0; }
.grid-item.active {
  background: #3A7BD5;
  color: #fff;
}
.status-text {
  font-size: 22rpx;
  color: #888;
  margin-top: 8rpx;
  word-break: break-all;
}
.quick-row {
  display: flex;
  margin-top: 8rpx;
}
.mini-btn {
  flex: 1;
  margin-right: 16rpx;
  font-size: 26rpx;
  background: #f0f3f8;
  border-radius: 10rpx;
  line-height: 2.4;
}
.mini-btn:last-child { margin-right: 0; }
.mini-btn.primary { background: #3A7BD5; color: #fff; }
.log {
  height: 360rpx;
  background: #1d2129;
  border-radius: 10rpx;
  padding: 12rpx;
}
.log-item {
  display: flex;
  font-size: 22rpx;
  color: #9cdcfe;
  margin-bottom: 8rpx;
  word-break: break-all;
}
.log-item.incoming .log-text { color: #b5e8a1; }
.log-time { color: #666; margin-right: 12rpx; flex-shrink: 0; }
.log-text { flex: 1; }
.log-empty { color: #666; font-size: 22rpx; text-align: center; padding: 40rpx 0; }
</style>
