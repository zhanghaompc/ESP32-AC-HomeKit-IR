<template>
  <view class="container">
    <view class="header">
      <text class="title">定时任务管理</text>
      <view class="ble-status" :class="isConnected ? 'connected' : 'disconnected'">
        <text>{{ isConnected ? '● 已连接' : '○ 未连接' }}</text>
      </view>
    </view>

    <view class="time-card">
      <text class="time-label">当前时间（手机本地）</text>
      <text class="time-value">{{ currentTime || '--:--:--' }}</text>
      <text class="time-date">{{ currentDate || '--' }}</text>
      <button class="sync-time-btn" :disabled="!isConnected" @tap="syncDeviceTime">
        ↻ 同步时间到设备
      </button>
    </view>

    <view class="section">
      <view class="section-header">
        <text class="section-title">定时任务</text>
        <button class="refresh-btn" :disabled="!isConnected" @tap="refreshTimerList">
          ↻ 刷新
        </button>
      </view>

      <view class="timer-list">
        <view
          v-for="(item, index) in timerList"
          :key="item.id"
          class="timer-item"
          :class="item.enabled ? 'enabled' : 'disabled'"
          @tap="editTimer(item)"
        >
          <view class="timer-id">
            <text class="id-number">{{ index + 1 }}</text>
          </view>
          <view class="timer-info">
            <view class="timer-time">
              <text class="time">{{ item.hour < 10 ? '0' + item.hour : item.hour }}:{{ item.minute < 10 ? '0' + item.minute : item.minute }}</text>
              <text class="repeat">{{ item.repeat ? '重复' : '单次' }}</text>
              <text class="power-tag" :class="isTaskPowerOn(item.power) ? 'on' : 'off'">
                {{ isTaskPowerOn(item.power) ? '开机' : '关机' }}
              </text>
            </view>
            <view v-if="isTaskPowerOn(item.power)" class="timer-details">
              <text>温度: {{ item.temp }}°C</text>
              <text>模式: {{ modeList[item.mode] }}</text>
              <text>风速: {{ speedList[item.speed] }}</text>
            </view>
            <view v-else class="timer-details">
              <text>仅关机任务，不设温度</text>
            </view>
          </view>
          <view class="timer-actions" @tap.stop="noop">
            <switch
              :checked="item.enabled"
              @change="toggleTimerEnable(item.id, $event)"
            />
            <button class="delete-btn" @tap="deleteTimer(item.id)">删除</button>
          </view>
        </view>
      </view>

      <view v-if="timerList.length === 0 && isConnected" class="empty-state">
        <text class="empty-icon">🕐</text>
        <text class="empty-text">暂无定时任务</text>
        <text class="empty-subtext">点击下方按钮添加定时任务</text>
      </view>

      <view v-if="!isConnected" class="empty-state">
        <text class="empty-icon">📡</text>
        <text class="empty-text">设备未连接</text>
        <text class="empty-subtext">请返回首页重新连接后，再管理定时任务</text>
        <button class="back-home-btn" @tap="goBackHome">返回首页</button>
      </view>
    </view>

    <button class="add-timer-btn" :disabled="!isConnected" @tap="openAddModal">
      ＋ 添加定时任务
    </button>

    <button class="wifi-mode-btn" :disabled="!isConnected" @tap="toggleWifiMode">
      ⚡ 切换到WiFi模式
    </button>

    <view v-if="isLoading" class="loading-overlay">
      <view class="loading-spinner">
        <view class="spinner"></view>
        <text class="loading-text">{{ loadingText || '加载中...' }}</text>
      </view>
    </view>

    <view v-if="status" class="status-toast" :class="statusType">
      {{ status }}
    </view>

    <view v-if="showAddModal" class="modal-overlay" @tap="hideAddModal">
      <view class="modal" @tap.stop="noop">
        <view class="modal-header">
          <text>{{ editingTimer ? '编辑定时任务 #' + currentEditingId : '添加定时任务' }}</text>
          <text class="close-text" @tap="hideAddModal">× 关闭</text>
        </view>
        <view class="modal-content">
          <view class="form-item">
            <text class="label">执行时间</text>
            <view class="time-picker">
              <picker
                class="picker-item"
                mode="selector"
                :range="hourList"
                :value="newTimer.hour"
                @change="onHourChange"
              >
                <text class="picker-text">{{ hourList[newTimer.hour] }}</text>
              </picker>
              <text class="time-separator">:</text>
              <picker
                class="picker-item"
                mode="selector"
                :range="minuteList"
                :value="newTimer.minute"
                @change="onMinuteChange"
              >
                <text class="picker-text">{{ minuteList[newTimer.minute] }}</text>
              </picker>
            </view>
          </view>

          <view class="form-item">
            <text class="label">重复设置</text>
            <view class="toggle-container">
              <switch :checked="newTimer.repeat" @change="onRepeatChange" />
              <text>{{ newTimer.repeat ? '每天重复' : '仅执行一次' }}</text>
            </view>
          </view>

          <view class="form-item">
            <text class="label">开关状态</text>
            <view class="power-toggle">
              <view
                class="power-option"
                :class="{ active: newTimer.power === 'on' }"
                @tap="setPowerOn"
              >
                <text>开机</text>
              </view>
              <view
                class="power-option"
                :class="{ active: newTimer.power === 'off' }"
                @tap="setPowerOff"
              >
                <text>关机</text>
              </view>
            </view>
          </view>

          <view v-if="newTimer.power === 'on'" class="form-item">
            <text class="label">温度设置</text>
            <view class="temp-control">
              <button class="temp-button" @tap="decreaseTemp">−</button>
              <text class="temp-value">{{ newTimer.temp }}°C</text>
              <button class="temp-button" @tap="increaseTemp">+</button>
            </view>
          </view>

          <view v-if="newTimer.power === 'on'" class="form-item">
            <text class="label">模式选择</text>
            <button class="mode-select-btn" @tap="openModeSelector">
              {{ modeList[newTimer.mode] }} ▾
            </button>
          </view>

          <view v-if="newTimer.power === 'on'" class="form-item">
            <text class="label">风速选择</text>
            <button class="speed-select-btn" @tap="openSpeedSelector">
              {{ speedList[newTimer.speed] }} ▾
            </button>
          </view>

          <view class="modal-footer">
            <button class="cancel-btn" @tap="hideAddModal">取消</button>
            <button class="confirm-btn" @tap="saveTimer">{{ editingTimer ? '保存' : '添加' }}</button>
          </view>
        </view>
      </view>
    </view>

    <view v-if="showModeSelector" class="selector-overlay" @tap="hideModeSelector">
      <view class="selector-container" @tap.stop="noop">
        <view class="selector-header">
          <text>选择模式</text>
          <text class="close-text" @tap="hideModeSelector">×</text>
        </view>
        <view class="selector-content">
          <view
            v-for="(item, index) in modeList"
            :key="item"
            class="selector-item"
            :class="{ active: newTimer.mode === index }"
            @tap="selectMode(index)"
          >
            <text>{{ item }}</text>
            <text v-if="newTimer.mode === index">✓</text>
          </view>
        </view>
      </view>
    </view>

    <view v-if="showSpeedSelector" class="selector-overlay" @tap="hideSpeedSelector">
      <view class="selector-container" @tap.stop="noop">
        <view class="selector-header">
          <text>选择风速</text>
          <text class="close-text" @tap="hideSpeedSelector">×</text>
        </view>
        <view class="selector-content">
          <view
            v-for="(item, index) in speedList"
            :key="item"
            class="selector-item"
            :class="{ active: newTimer.speed === index }"
            @tap="selectSpeed(index)"
          >
            <text>{{ item }}</text>
            <text v-if="newTimer.speed === index">✓</text>
          </view>
        </view>
      </view>
    </view>
  </view>
</template>

<script>
import { MODE_LIST, SPEED_LIST, BLE_CONFIG } from '../../utils/constants.js'
import {
  normalizeUUID,
  stringToArrayBuffer,
  arrayBufferToString,
  ensureNativePermissions,
  enqueueWrite
} from '../../utils/ble-utils.js'

export default {
  data() {
    return {
      isConnected: false,
      deviceId: '',
      serviceUUID: BLE_CONFIG.serviceUUID,
      rxCharUUID: BLE_CONFIG.rxCharUUID,
      txCharUUID: BLE_CONFIG.txCharUUID,
      targetServiceId: '',
      rxCharId: '',
      txCharId: '',

      currentTime: '',
      currentDate: '',
      timerList: [],
      modeList: MODE_LIST,
      speedList: SPEED_LIST,

      showAddModal: false,
      showModeSelector: false,
      showSpeedSelector: false,
      hourList: Array.from({ length: 24 }, (_, i) => i + '时'),
      minuteList: Array.from({ length: 60 }, (_, i) => i + '分'),
      newTimer: {
        hour: 8,
        minute: 0,
        temp: 25,
        mode: 0,
        speed: 0,
        power: 'on',
        repeat: true
      },

      editingTimer: false,
      currentEditingId: null,
      isLoading: false,
      loadingText: '加载中...',
      status: '',
      statusType: '',

      commandQueue: [],
      isSending: false,
      lastCommandTime: 0,
      commandInterval: 800,
      maxQueueLength: 20,
      maxCommandRetries: 3,
      timeRefreshTimer: null,
      dataBuffer: '',
      bufferTimer: null,
      _onData: null,
      _ownConnection: false,
      _connTimer: null,
      _opTimer: null,
      _connHandler: null
    }
  },

  onLoad() {
    this.bindConnectionListener()
    const globalBle = getApp().globalData.ble
    if (globalBle && globalBle.connected && globalBle.deviceId) {
      this.adoptGlobalConnection(globalBle)
    } else {
      const savedDeviceId = uni.getStorageSync('savedDeviceId')
      if (savedDeviceId) {
        this.deviceId = savedDeviceId
        this.connectBLE(savedDeviceId)
      } else {
        this.showStatus('请先返回首页连接设备', 'fail')
      }
    }
  },

  onShow() {
    const globalBle = getApp().globalData.ble
    if (globalBle && globalBle.connected) {
      if (!this.isConnected) this.adoptGlobalConnection(globalBle)
    } else if (this.isConnected) {
      this.isConnected = false
      this.showStatus('设备已断开', 'fail')
    }
  },

  onUnload() {
    this.clearTimeRefreshTimer()
    if (this._connHandler) {
      try {
        uni.offBLEConnectionStateChange(this._connHandler)
      } catch (e) {
        console.warn('清理连接监听失败:', e)
      }
      this._connHandler = null
    }
    if (this._connTimer) {
      clearTimeout(this._connTimer)
      this._connTimer = null
    }
    if (this._opTimer) {
      clearTimeout(this._opTimer)
      this._opTimer = null
    }
    if (this.bufferTimer) {
      clearTimeout(this.bufferTimer)
      this.bufferTimer = null
    }
    if (this._onData) {
      try {
        uni.offBLECharacteristicValueChange(this._onData)
      } catch (e) {
        console.warn('清理监听失败:', e)
      }
      this._onData = null
    }
    if (this.isConnected && this._ownConnection) {
      uni.closeBLEConnection({
        deviceId: this.deviceId,
        fail: (err) => console.warn('断开连接失败:', err)
      })
      getApp().globalData.ble = {
        connected: false,
        deviceId: '',
        serviceId: '',
        rxCharId: '',
        txCharId: ''
      }
    }
  },

  methods: {
    adoptGlobalConnection(g) {
      this.isConnected = true
      this._ownConnection = false
      this.deviceId = g.deviceId
      this.bindDataListener()

      if (!g.serviceId || !g.rxCharId || !g.txCharId) {
        this.showStatus('设备已连接，正在初始化…', '')
        this.getDeviceServices(g.deviceId)
        return
      }

      this.targetServiceId = g.serviceId
      this.rxCharId = g.rxCharId
      this.txCharId = g.txCharId
      this.showStatus('设备已连接', 'success')
      this.refreshTime()
      this.refreshTimerList()
      this.startTimeRefreshTimer()
      this.syncDeviceTime()
    },

    bindDataListener() {
      if (this._onData) return
      this._onData = (res) => {
        if (res.characteristicId === this.txCharId) {
          this.processData(res.value)
        }
      }
      uni.onBLECharacteristicValueChange(this._onData)
    },

    // 监听连接断开/恢复：断开时立即把页面置为“未连接”
    bindConnectionListener() {
      if (this._connHandler) return
      this._connHandler = (res) => {
        if (!res.connected) {
          this.isConnected = false
          this._ownConnection = false
          this.clearTimeRefreshTimer()
          this.hideLoading()
          this.showStatus('设备连接已断开，请返回首页重新连接', 'fail')
        } else {
          const g = getApp().globalData.ble
          if (g && g.connected && !this.isConnected) {
            this.adoptGlobalConnection(g)
          }
        }
      }
      uni.onBLEConnectionStateChange(this._connHandler)
    },

    async connectBLE(deviceId) {
      this.showLoading('正在连接设备...')
      await ensureNativePermissions()

      // 连接兜底：15 秒内没有回调就停止转圈并提示
      this._connTimer = setTimeout(() => {
        if (this.isLoading) {
          uni.closeBLEConnection({
            deviceId,
            fail: () => {}
          })
          this.hideLoading()
          this.showStatus('连接超时，请确认设备已开机并靠近手机', 'fail')
        }
        this._connTimer = null
      }, 15000)

      uni.createBLEConnection({
        deviceId,
        timeout: 15000,
        success: () => {
          if (this._connTimer) {
            clearTimeout(this._connTimer)
            this._connTimer = null
          }
          this.isConnected = true
          this._ownConnection = true
          this.deviceId = deviceId
          this.showStatus('设备连接成功', 'success')
          this.getDeviceServices(deviceId)
        },
        fail: (err) => {
          if (this._connTimer) {
            clearTimeout(this._connTimer)
            this._connTimer = null
          }
          this.showStatus('连接失败: ' + err.errMsg, 'fail')
          this.hideLoading()
        }
      })
    },

    getDeviceServices(deviceId) {
      uni.getBLEDeviceServices({
        deviceId,
        success: (res) => {
          let targetService = null
          for (let i = 0; i < res.services.length; i++) {
            if (normalizeUUID(res.services[i].uuid) === normalizeUUID(this.serviceUUID)) {
              targetService = res.services[i]
              break
            }
          }
          if (targetService) {
            this.targetServiceId = targetService.uuid
            this.getDeviceCharacteristics(deviceId, targetService.uuid)
          } else {
            this.showStatus('未找到目标服务', 'fail')
            this.hideLoading()
          }
        },
        fail: () => {
          this.showStatus('获取服务失败', 'fail')
          this.hideLoading()
        }
      })
    },

    getDeviceCharacteristics(deviceId, serviceId) {
      uni.getBLEDeviceCharacteristics({
        deviceId,
        serviceId,
        success: (res) => {
          let rxCharId = ''
          let txCharId = ''
          for (let i = 0; i < res.characteristics.length; i++) {
            const char = res.characteristics[i]
            if (normalizeUUID(char.uuid) === normalizeUUID(this.rxCharUUID)) {
              rxCharId = char.uuid
            } else if (normalizeUUID(char.uuid) === normalizeUUID(this.txCharUUID)) {
              txCharId = char.uuid
            }
          }

          if (rxCharId && txCharId) {
            this.rxCharId = rxCharId
            this.txCharId = txCharId
            this.enableNotification(deviceId, serviceId, txCharId)
            getApp().globalData.ble = {
              connected: true,
              deviceId: this.deviceId,
              serviceId: serviceId,
              rxCharId: rxCharId,
              txCharId: txCharId
            }
            this.showStatus('蓝牙初始化完成', 'success')
            setTimeout(() => {
              this.refreshTime()
              this.refreshTimerList()
              this.startTimeRefreshTimer()
              this.syncDeviceTime()
            }, 500)
            this.hideLoading()
          } else {
            this.showStatus('未找到全部所需特征', 'fail')
            this.hideLoading()
          }
        },
        fail: () => {
          this.showStatus('获取特征失败', 'fail')
          this.hideLoading()
        }
      })
    },

    enableNotification(deviceId, serviceId, characteristicId) {
      uni.notifyBLECharacteristicValueChange({
        deviceId,
        serviceId,
        characteristicId,
        state: true,
        success: () => console.log('启用通知成功'),
        fail: (err) => console.error('启用通知失败:', err)
      })

      this.bindDataListener()
    },

    processData(buffer) {
      const data = arrayBufferToString(buffer)
      console.log('[BLE接收] 原始数据:', data)
      this.dataBuffer += data
      this.processBufferedData()
    },

    processBufferedData() {
      let idx
      while ((idx = this.dataBuffer.indexOf('\n')) !== -1) {
        const line = this.dataBuffer.slice(0, idx)
        this.dataBuffer = this.dataBuffer.slice(idx + 1)
        if (line.trim()) this.handleMessage(line.trim())
      }

      const buf = this.dataBuffer
      if (!buf) {
        if (this.bufferTimer) {
          clearTimeout(this.bufferTimer)
          this.bufferTimer = null
        }
        return
      }

      // 任务列表 JSON 必须能完整解析才消费，否则继续等后续分片
      if (buf.startsWith('timers=')) {
        try {
          JSON.parse(buf.substring(7))
          this.dataBuffer = ''
          if (this.bufferTimer) {
            clearTimeout(this.bufferTimer)
            this.bufferTimer = null
          }
          this.handleMessage(buf)
        } catch (e) {
          this.scheduleBufferTimeout(1500)
        }
        return
      }

      if (this.isFinalMessage(buf)) {
        this.dataBuffer = ''
        if (this.bufferTimer) {
          clearTimeout(this.bufferTimer)
          this.bufferTimer = null
        }
        this.handleMessage(buf)
        return
      }

      // protocol= / learn=success: 可能是长消息的分片（新固件），等 300ms 看后续分片
      if (this.isAmbiguousMessage(buf)) {
        this.scheduleBufferTimeout(300)
        return
      }

      this.scheduleBufferTimeout(2000)
    },

    isFinalMessage(buf) {
      return (
        /^t[\d.]+h[\d.]+$/.test(buf) ||
        /^power=(on|off)$/.test(buf) ||
        /^time=(ok|invalid|未同步)$/.test(buf) ||
        /^time=\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/.test(buf) ||
        /^learn=(waiting|timeout)$/.test(buf) ||
        /^wifi=unsupported$/.test(buf) ||
        /^timer_(add|delete|enable|update)=(success|failed)(;id=\d+)?$/.test(buf)
      )
    },

    isAmbiguousMessage(buf) {
      return /^protocol=\w+$/.test(buf) || /^learn=success:\w+$/.test(buf)
    },

    scheduleBufferTimeout(ms = 2000) {
      if (this.bufferTimer) clearTimeout(this.bufferTimer)
      this.bufferTimer = setTimeout(() => {
        this.bufferTimer = null
        if (!this.dataBuffer) return
        // 超时后仍未等到 \n：如果是完整模式（兼容旧固件）就消费，否则丢弃
        if (this.isFinalMessage(this.dataBuffer) || this.isAmbiguousMessage(this.dataBuffer)) {
          const buf = this.dataBuffer
          this.dataBuffer = ''
          this.handleMessage(buf)
          return
        }
        console.warn('BLE回包不完整，已丢弃:', this.dataBuffer)
        if (this.dataBuffer.startsWith('timers=')) {
          this.hideLoading()
          this.showStatus('任务列表解析失败，请重试或检查固件版本', 'fail')
        } else if (this.dataBuffer.startsWith('unknown_cmd:')) {
          this.hideLoading()
          this.showStatus('设备固件版本过旧，请重新烧录固件后重试', 'fail')
        } else if (
          this.dataBuffer.startsWith('timer_') ||
          this.dataBuffer.startsWith('learn=')
        ) {
          this.hideLoading()
          this.showStatus('设备回包不完整（BLE 单包限制），请升级固件', 'fail')
        }
        this.dataBuffer = ''
      }, ms)
    },

    handleMessage(data) {
      try {
        const timeMatch = data.match(/^time=(.+)$/)
        if (timeMatch) {
          // 界面时间使用手机本地时间，设备返回的时间只做日志，不覆盖显示
          console.log('设备时间(忽略):', timeMatch[1])
          return
        }

        const wifiInfo = data.match(/^wifi=(.+)$/)
        if (wifiInfo) {
          if (wifiInfo[1] === 'unsupported') {
            this.showStatus('当前固件不支持WiFi模式', 'fail')
          } else {
            this.showStatus('WiFi模式响应: ' + wifiInfo[1], '')
          }
          return
        }

        const timerListMatch = data.match(/timers=(.+)/)
        if (timerListMatch) {
          try {
            this.timerList = JSON.parse(timerListMatch[1])
            this.showStatus('定时任务列表已更新', 'success')
          } catch (e) {
            this.showStatus('解析任务列表失败', 'fail')
          }
          this.hideLoading()
          return
        }

        const addMatch = data.match(/timer_add=(\w+);id=(\d+)/)
        if (addMatch) {
          if (addMatch[1] === 'success') {
            this.showStatus('定时任务添加成功', 'success')
            this.hideAddModal()
            this.refreshTimerList()
          } else {
            this.showStatus('添加任务失败', 'fail')
          }
          this.hideLoading()
          return
        }

        const updateMatch = data.match(/timer_update=(\w+);id=(\d+)/)
        if (updateMatch) {
          if (updateMatch[1] === 'success') {
            this.showStatus('定时任务更新成功', 'success')
            this.hideAddModal()
            this.refreshTimerList()
          } else {
            this.showStatus('更新任务失败', 'fail')
          }
          this.hideLoading()
          return
        }

        const deleteMatch = data.match(/timer_delete=(\w+);id=(\d+)/)
        if (deleteMatch) {
          if (deleteMatch[1] === 'success') {
            this.showStatus('定时任务删除成功', 'success')
            this.refreshTimerList()
            this.hideLoading()
          } else {
            this.showStatus('删除任务失败', 'fail')
            this.hideLoading()
          }
          return
        }

        const enableMatch = data.match(/timer_enable=(\w+);id=(\d+)/)
        if (enableMatch) {
          if (enableMatch[1] === 'success') {
            this.showStatus('任务状态已更新', 'success')
            this.refreshTimerList()
          } else {
            this.showStatus('更新任务状态失败', 'fail')
          }
          this.hideLoading()
          return
        }
      } catch (error) {
        console.error('数据解析错误:', error)
      }
    },

    sendCommand(command) {
      if (!this.isConnected || !this.targetServiceId || !this.rxCharId) {
        this.showStatus('设备未连接', 'fail')
        return
      }

      const now = Date.now()
      if (now - this.lastCommandTime < this.commandInterval) {
        setTimeout(
          () => this.sendCommand(command),
          this.commandInterval - (now - this.lastCommandTime)
        )
        return
      }

      this.lastCommandTime = now
      enqueueWrite(() => new Promise((resolve) => {
        uni.writeBLECharacteristicValue({
          deviceId: this.deviceId,
          serviceId: this.targetServiceId,
          characteristicId: this.rxCharId,
          value: stringToArrayBuffer(command),
          success: () => {
            console.log('命令发送成功:', command)
            resolve()
          },
          fail: (err) => {
            console.error('命令发送失败:', err)
            this.hideLoading()
            if (
              err.errCode === 10008 ||
              err.errCode === 10005 ||
              /disconnect/i.test(err.errMsg || '')
            ) {
              this.isConnected = false
              this.showStatus('设备连接已断开，请返回首页重新连接', 'fail')
            } else {
              this.showStatus('发送失败: ' + err.errMsg, 'fail')
            }
            resolve()
          }
        })
      }))
    },

    // 时间显示改用手机本地时间，不再依赖设备返回
    refreshTime() {
      const d = new Date()
      const pad = (n) => (n < 10 ? '0' + n : '' + n)
      this.currentDate = `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`
      this.currentTime = `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
    },

    // 把手机本地时间写入设备 RTC（需固件支持 time=YYYY-MM-DD HH:MM:SS 命令）
    syncDeviceTime() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      const d = new Date()
      const pad = (n) => (n < 10 ? '0' + n : '' + n)
      const timeStr = `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
      this.sendCommand(`time=${timeStr}`)
      this.showStatus('正在同步时间到设备…', '')
    },

    refreshTimerList() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      this.showLoadingWithTimeout('加载任务列表...', 5000)
      this.sendCommand('timer=list')
    },

    openAddModal() {
      this.showAddModal = true
      this.editingTimer = false
      this.currentEditingId = null
      this.newTimer = {
        hour: 8,
        minute: 0,
        temp: 25,
        mode: 0,
        speed: 0,
        power: 'on',
        repeat: true
      }
    },

    editTimer(item) {
      this.showAddModal = true
      this.editingTimer = true
      this.currentEditingId = item.id
      this.newTimer = {
        hour: item.hour,
        minute: item.minute,
        temp: item.temp || 25,
        mode: item.mode || 0,
        speed: item.speed || 0,
        power: item.power ? 'on' : 'off',
        repeat: item.repeat
      }
    },

    hideAddModal() {
      this.showAddModal = false
      this.editingTimer = false
      this.currentEditingId = null
    },

    addTimer() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      const timer = this.newTimer
      const power = timer.power === 'on' || timer.power === true ? 'on' : 'off'
      const command =
        power === 'on'
          ? `timer=add;hour=${timer.hour};minute=${timer.minute};temp=${timer.temp};mode=${timer.mode};speed=${timer.speed};power=${power};repeat=${timer.repeat ? 1 : 0}`
          : `timer=add;hour=${timer.hour};minute=${timer.minute};power=${power};repeat=${timer.repeat ? 1 : 0}`
      this.showLoadingWithTimeout('添加任务...', 8000)
      this.sendCommand(command)
    },

    updateTimer() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      const timer = this.newTimer
      const id = this.currentEditingId
      const power = timer.power === 'on' || timer.power === true ? 'on' : 'off'
      const command =
        power === 'on'
          ? `timer=update;id=${id};hour=${timer.hour};minute=${timer.minute};temp=${timer.temp};mode=${timer.mode};speed=${timer.speed};power=${power};repeat=${timer.repeat ? 1 : 0}`
          : `timer=update;id=${id};hour=${timer.hour};minute=${timer.minute};power=${power};repeat=${timer.repeat ? 1 : 0}`
      this.showLoadingWithTimeout('更新任务...', 8000)
      this.sendCommand(command)
    },

    deleteTimer(id) {
      uni.showModal({
        title: '确认删除',
        content: '确定要删除这个定时任务吗？',
        success: (res) => {
          if (res.confirm) {
            this.showLoadingWithTimeout('删除任务...', 8000)
            this.sendCommand(`timer=delete;id=${id}`)
          }
        }
      })
    },

    toggleTimerEnable(id, e) {
      const enabled = e.detail.value
      const state = enabled ? 1 : 0
      this.sendCommand(`timer=enable;id=${id};state=${state}`)
    },

    toggleWifiMode() {
      this.showStatus('正在切换到WiFi模式...', '')
      if (this.isConnected && this.targetServiceId && this.rxCharId) {
        this.sendCommand('wifi_mode')
      }
    },

    openModeSelector() {
      this.showModeSelector = true
    },
    hideModeSelector() {
      this.showModeSelector = false
    },
    selectMode(index) {
      this.newTimer.mode = index
      this.showModeSelector = false
    },
    openSpeedSelector() {
      this.showSpeedSelector = true
    },
    hideSpeedSelector() {
      this.showSpeedSelector = false
    },
    selectSpeed(index) {
      this.newTimer.speed = index
      this.showSpeedSelector = false
    },

    saveTimer() {
      if (this.editingTimer) this.updateTimer()
      else this.addTimer()
    },

    startTimeRefreshTimer() {
      this.clearTimeRefreshTimer()
      this.refreshTime()
      this.timeRefreshTimer = setInterval(() => {
        this.refreshTime()
      }, 1000)
    },

    clearTimeRefreshTimer() {
      if (this.timeRefreshTimer) {
        clearInterval(this.timeRefreshTimer)
        this.timeRefreshTimer = null
      }
    },

    onHourChange(e) {
      this.newTimer.hour = parseInt(e.detail.value)
    },
    onMinuteChange(e) {
      this.newTimer.minute = parseInt(e.detail.value)
    },
    onRepeatChange(e) {
      this.newTimer.repeat = e.detail.value
    },
    onPowerChange(e) {
      this.newTimer.power = e.detail.value
    },
    onTempChange(e) {
      this.newTimer.temp = parseInt(e.detail.value)
    },

    increaseTemp() {
      if (this.newTimer.temp < 30) this.newTimer.temp += 1
    },
    decreaseTemp() {
      if (this.newTimer.temp > 16) this.newTimer.temp -= 1
    },
    setPowerOn() {
      this.newTimer.power = 'on'
    },
    setPowerOff() {
      this.newTimer.power = 'off'
    },

    noop() {},

    isTaskPowerOn(power) {
      return power === true || power === 'on'
    },

    goBackHome() {
      uni.navigateBack({
        fail: () => uni.reLaunch({ url: '/pages/index/index' })
      })
    },

    showLoading(text) {
      this.isLoading = true
      this.loadingText = text
    },
    showLoadingWithTimeout(text, ms) {
      this.showLoading(text)
      if (this._opTimer) clearTimeout(this._opTimer)
      this._opTimer = setTimeout(() => {
        this._opTimer = null
        if (this.isLoading) {
          this.hideLoading()
          this.showStatus('操作超时，请重试', 'fail')
        }
      }, ms || 8000)
    },
    hideLoading() {
      this.isLoading = false
    },
    showStatus(message, type) {
      this.status = message
      this.statusType = type || ''
      setTimeout(() => {
        if (this.status === message) this.status = ''
      }, type === 'fail' ? 5000 : 3000)
    }
  }
}
</script>

<style>
.time-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8rpx;
  padding: 34rpx 24rpx;
  background: linear-gradient(150deg, #f0f6ff 0%, #ffffff 55%, #effaf5 100%);
  border-radius: 24rpx;
  box-shadow: 0 14rpx 36rpx rgba(37, 99, 235, 0.10);
  border: 1rpx solid rgba(255, 255, 255, 0.8);
  margin-bottom: 24rpx;
}

.time-label {
  font-size: 24rpx;
  color: #6b7280;
}

.time-value {
  font-size: 52rpx;
  font-weight: 700;
  color: #111827;
}

.time-date {
  font-size: 26rpx;
  color: #6b7280;
}

.sync-time-btn {
  margin-top: 12rpx;
  padding: 0 28rpx;
  font-size: 26rpx;
  color: #3a7bd5;
  background: #e8f1ff;
  border-radius: 999rpx;
  line-height: 2.4;
}

.section {
  background: #ffffff;
  border-radius: 24rpx;
  box-shadow: 0 14rpx 36rpx rgba(37, 99, 235, 0.10);
  border: 1rpx solid rgba(255, 255, 255, 0.8);
  padding: 8rpx 24rpx 24rpx;
  margin-bottom: 24rpx;
}

.section-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 22rpx 0;
}

.section-title {
  position: relative;
  padding-left: 20rpx;
  font-size: 30rpx;
  font-weight: 700;
  color: #111827;
}

.section-title::before {
  content: '';
  position: absolute;
  left: 0;
  top: 50%;
  transform: translateY(-50%);
  width: 8rpx;
  height: 30rpx;
  border-radius: 8rpx;
  background: linear-gradient(180deg, #3a7bd5, #5eead4);
}

.refresh-btn {
  padding: 0 18rpx;
  font-size: 24rpx;
  color: #3a7bd5;
  background: #e8f1ff;
  border-radius: 999rpx;
  line-height: 2.2;
}

.timer-item {
  display: flex;
  align-items: center;
  gap: 18rpx;
  padding: 22rpx 0;
  border-bottom: 1rpx solid #f1f5f9;
}

.timer-item.disabled {
  opacity: 0.55;
}

.timer-id {
  width: 52rpx;
  height: 52rpx;
  border-radius: 50%;
  background: #e8f1ff;
  display: flex;
  align-items: center;
  justify-content: center;
}

.id-number {
  color: #3a7bd5;
  font-weight: 700;
  font-size: 26rpx;
}

.timer-info {
  flex: 1;
}

.timer-time {
  display: flex;
  align-items: baseline;
  gap: 12rpx;
}

.timer-time .time {
  font-size: 34rpx;
  font-weight: 700;
  color: #111827;
}

.timer-time .repeat {
  font-size: 22rpx;
  color: #6b7280;
}

.timer-time .power-tag {
  font-size: 20rpx;
  padding: 3rpx 12rpx;
  border-radius: 999rpx;
}

.timer-time .power-tag.on {
  color: #16a34a;
  background: #e9f9ef;
}

.timer-time .power-tag.off {
  color: #ef4444;
  background: #fef2f2;
}

.timer-details {
  display: flex;
  flex-wrap: wrap;
  gap: 14rpx;
  margin-top: 8rpx;
  font-size: 22rpx;
  color: #6b7280;
}

.timer-actions {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 12rpx;
}

.delete-btn {
  padding: 0 16rpx;
  font-size: 22rpx;
  color: #ef4444;
  background: #fef2f2;
  border-radius: 999rpx;
  line-height: 2;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8rpx;
  padding: 48rpx 0 28rpx;
}

.empty-icon {
  font-size: 56rpx;
}

.empty-text {
  font-size: 28rpx;
  color: #374151;
}

.empty-subtext {
  font-size: 24rpx;
  color: #9ca3af;
}

.back-home-btn {
  margin-top: 20rpx;
  padding: 0 40rpx;
  color: #ffffff;
  background: linear-gradient(135deg, #6ba6f5, #3a7bd5);
  border-radius: 999rpx;
  font-size: 26rpx;
  line-height: 2.3;
  box-shadow: 0 10rpx 24rpx rgba(58, 123, 213, 0.24);
}

.add-timer-btn {
  color: #ffffff;
  background: linear-gradient(135deg, #6ba6f5, #3a7bd5);
  box-shadow: 0 12rpx 28rpx rgba(58, 123, 213, 0.28);
  border-radius: 22rpx;
  font-size: 30rpx;
  font-weight: 600;
  line-height: 2.4;
  margin-bottom: 18rpx;
}

.wifi-mode-btn {
  color: #475569;
  background: linear-gradient(135deg, #ffffff, #f1f5f9);
  border: 1rpx solid rgba(100, 116, 139, 0.18);
  border-radius: 22rpx;
  font-size: 28rpx;
  line-height: 2.3;
}

.status-toast {
  position: fixed;
  left: 50%;
  bottom: 60rpx;
  transform: translateX(-50%);
  padding: 18rpx 28rpx;
  border-radius: 999rpx;
  background: rgba(17, 24, 39, 0.9);
  color: #ffffff;
  font-size: 26rpx;
  z-index: 1002;
}

.status-toast.success {
  background: rgba(21, 128, 61, 0.92);
}

.status-toast.fail {
  background: rgba(185, 28, 28, 0.92);
}

.modal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.close-text {
  font-size: 24rpx;
  color: #9ca3af;
}

.modal-content {
  padding: 8rpx 34rpx 30rpx;
  max-height: 70vh;
  overflow-y: auto;
}

.form-item {
  padding: 20rpx 0;
  border-bottom: 1rpx solid #f1f5f9;
}

.form-item:last-child {
  border-bottom: none;
}

.label {
  display: block;
  font-size: 24rpx;
  color: #6b7280;
  margin-bottom: 14rpx;
}

.time-picker {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 20rpx;
}

.picker-item {
  flex: 1;
}

.picker-text {
  display: block;
  text-align: center;
  padding: 16rpx;
  background: #f1f5f9;
  border-radius: 14rpx;
  font-size: 30rpx;
}

.time-separator {
  font-size: 34rpx;
  font-weight: 700;
}

.toggle-container {
  display: flex;
  align-items: center;
  gap: 16rpx;
  font-size: 26rpx;
  color: #374151;
}

.power-toggle {
  display: flex;
  gap: 16rpx;
}

.power-option {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8rpx;
  padding: 18rpx;
  border: 2rpx solid #e5e7eb;
  border-radius: 16rpx;
  font-size: 26rpx;
}

.power-option.active {
  border-color: #3a7bd5;
  color: #3a7bd5;
  background: #f0f6ff;
}

.temp-control {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 30rpx;
}

.temp-button {
  width: 88rpx;
  height: 72rpx;
  line-height: 72rpx;
  border-radius: 16rpx;
  background: #f1f5f9;
  color: #111827;
  font-size: 36rpx;
}

.temp-value {
  font-size: 40rpx;
  font-weight: 700;
}

.mode-select-btn,
.speed-select-btn {
  width: 100%;
  text-align: left;
  padding: 18rpx 22rpx;
  background: #f1f5f9;
  border-radius: 14rpx;
  font-size: 28rpx;
  color: #1f2937;
}

.modal-footer {
  display: flex;
  gap: 20rpx;
  padding-top: 24rpx;
}

.cancel-btn,
.confirm-btn {
  flex: 1;
  border-radius: 18rpx;
  font-size: 28rpx;
  line-height: 2.4;
}

.cancel-btn {
  color: #4b5563;
  background: #f1f5f9;
}

.confirm-btn {
  color: #ffffff;
  background: linear-gradient(135deg, #6ba6f5, #3a7bd5);
  box-shadow: 0 10rpx 24rpx rgba(58, 123, 213, 0.24);
}

.selector-container {
  width: 620rpx;
  max-height: 70vh;
  background: #ffffff;
  border-radius: 24rpx;
  overflow: hidden;
}

.selector-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 28rpx 30rpx;
  font-size: 30rpx;
  font-weight: 700;
  border-bottom: 1rpx solid #eef2f7;
}

.selector-content {
  max-height: 60vh;
  overflow-y: auto;
}

/* ===== 方案一：精致蓝白（覆盖式优化） ===== */
.time-card {
  border-radius: 30rpx;
  box-shadow: 0 16rpx 40rpx rgba(37, 99, 235, 0.10);
}

.sync-time-btn {
  color: #2563eb;
  background: linear-gradient(135deg, #ffffff, #e8f2ff);
  border: 1rpx solid rgba(37, 99, 235, 0.18);
}

.section {
  border-radius: 28rpx;
  background: rgba(255, 255, 255, 0.9);
  box-shadow: 0 14rpx 36rpx rgba(37, 99, 235, 0.08);
}

.refresh-btn {
  color: #2563eb;
  background: linear-gradient(135deg, #ffffff, #e8f2ff);
}

.timer-item {
  border-bottom: 1rpx solid #f1f5f9;
}

.timer-id {
  background: linear-gradient(135deg, #dbeafe, #bfdbfe);
}

.id-number {
  color: #2563eb;
}

.back-home-btn,
.confirm-btn,
.add-timer-btn {
  background: linear-gradient(135deg, #38bdf8, #2563eb);
  box-shadow: 0 12rpx 26rpx rgba(37, 99, 235, 0.24);
}

.power-option.active {
  border-color: #2563eb;
  color: #2563eb;
  background: #eef4ff;
}

/* ===== premium-ui-builder-skill：组件状态 ===== */
.add-timer-btn:active,
.wifi-mode-btn:active,
.sync-time-btn:active,
.refresh-btn:active,
.back-home-btn:active,
.confirm-btn:active,
.cancel-btn:active,
.delete-btn:active,
.temp-button:active,
.mode-select-btn:active,
.speed-select-btn:active,
.power-option:active {
  transform: scale(0.97);
  opacity: 0.92;
}

.timer-item:active {
  background: #f7faff;
}

.add-timer-btn[disabled],
.wifi-mode-btn[disabled],
.sync-time-btn[disabled],
.refresh-btn[disabled],
.back-home-btn[disabled] {
  opacity: 0.45;
}

.add-timer-btn,
.wifi-mode-btn,
.sync-time-btn,
.refresh-btn,
.back-home-btn,
.confirm-btn,
.cancel-btn,
.delete-btn,
.temp-button,
.mode-select-btn,
.speed-select-btn,
.power-option,
.timer-item {
  transition: transform 0.12s ease-out, background 0.15s ease-out, opacity 0.15s ease-out;
}
</style>
