<template>
  <view class="container">
    <view class="header">
      <text class="title">{{ displayName }}</text>
      <view class="ble-status" :class="connPillClass">
        <text>{{ connPillText }}</text>
      </view>
    </view>

    <connect-panel
      v-if="connState !== 'connected'"
      :state="connState"
      :device-list="deviceList"
      :saved-device="savedDevice"
      :saved-device-id="savedDeviceId"
      :error="connError"
      @scan="beginScan"
      @stop-scan="stopScan"
      @connect-device="connectDevice"
      @connect-saved="connectDevice"
      @disconnect="disconnectBLE"
      @stop-reconnect="stopReconnect"
      @forget-device="forgetDevice"
      @open-settings="openSettings"
      @retry="handleRetry"
    />
    <view v-else class="connected-bar">
      <view class="connected-dot"></view>
      <text class="connected-text">{{ displayName }}</text>
      <text class="connected-action" @tap="disconnectBLE">断开</text>
    </view>

    <view class="ambient-row">
      <text class="ambient-item">🌡️ {{ temp || '--' }}°C</text>
      <text class="ambient-item">💧 {{ humidity || '--' }}%</text>
      <text class="ambient-item link" @tap="queryCurrentProtocol">协议 {{ protocolLabel(protocolList[protocolIndex]) || '--' }}</text>
    </view>

    <view class="control-card">
      <view class="power-row">
        <view>
          <text class="control-label">空调电源</text>
          <text class="power-state" :class="isPowerOn ? 'on' : 'off'">
            {{ isPowerOn ? '已开启' : '已关闭' }}
          </text>
        </view>
        <view
          class="switch"
          :class="{ on: isPowerOn }"
          :style="{ opacity: isConnected ? 1 : 0.4 }"
          @tap="togglePower"
        >
          <view class="switch-knob"></view>
        </view>
      </view>

      <view class="divider"></view>

      <view class="temp-row">
        <button class="step-btn" :disabled="!isConnected" @tap="decreaseTemp">−</button>
        <view class="temp-display">
          <text class="temp-num">{{ targetTemp }}</text>
          <text class="temp-unit">°C</text>
        </view>
        <button class="step-btn" :disabled="!isConnected" @tap="increaseTemp">+</button>
      </view>

      <view class="divider"></view>

      <view class="seg-block">
        <text class="control-label">模式</text>
        <view class="seg">
          <view
            v-for="(item, index) in modeList"
            :key="item"
            class="seg-item"
            :class="{ active: modeIndex === index }"
            @tap="onModeChange(index)"
          >{{ item }}</view>
        </view>
      </view>

      <view class="seg-block">
        <text class="control-label">风速</text>
        <view class="seg">
          <view
            v-for="(item, index) in speedList"
            :key="item"
            class="seg-item"
            :class="{ active: speedIndex === index }"
            @tap="onSpeedChange(index)"
          >{{ item }}</view>
        </view>
      </view>

      <view class="divider"></view>

      <view class="protocol-row" @tap="openProtocolPicker">
        <text class="control-label">空调协议</text>
        <view class="protocol-value">
          <text>{{ protocolLabel(protocolList[protocolIndex]) || '未选择' }}</text>
          <text class="chevron">›</text>
        </view>
      </view>
    </view>

    <view class="actions-row">
      <button class="mini-btn learn" :disabled="!isConnected || isLearning" @tap="startLearningMode">
        {{ isLearning ? '学习中…' : '协议学习' }}
      </button>
      <button class="mini-btn timer" :disabled="!isConnected" @tap="goToTimer">定时任务</button>
      <button class="mini-btn danger" :disabled="!isConnected" @tap="factoryReset">恢复出厂设置</button>
    </view>

    <view class="footer-area">
      <view class="more-info" hover-class="more-info-hover" @tap="goHelp">
        <text>更多信息</text>
        <text class="chevron">›</text>
      </view>
    </view>

    <view class="status-bar">
      <text v-if="status" class="status-text" :class="statusType">{{ status }}</text>
    </view>

    <view v-if="showProtocolPicker" class="modal-overlay" @tap="hideProtocolPicker">
      <view class="modal" @tap.stop="noop">
        <view class="modal-header">选择空调协议</view>
        <scroll-view class="modal-scroll" scroll-y>
          <view class="modal-options">
            <view
              v-for="(item, index) in protocolList"
              :key="item"
              class="option"
              :class="{ selected: protocolIndex === index }"
              @tap="onProtocolChange(index)"
            >
              <text>{{ protocolLabel(item) }}</text>
            </view>
          </view>
        </scroll-view>
      </view>
    </view>

    <view v-if="isLoading" class="loading-overlay">
      <view class="loading-spinner">
        <view class="spinner"></view>
        <text class="loading-text">{{ loadingText }}</text>
      </view>
    </view>
  </view>
</template>

<script>
import { PROTOCOL_LIST, PROTOCOL_CN, MODE_LIST, SPEED_LIST, BLE_CONFIG, DEVICE_DISPLAY_NAME } from '../../utils/constants.js'
import {
  normalizeUUID,
  stringToArrayBuffer,
  arrayBufferToString,
  ensureNativePermissions,
  enqueueWrite
} from '../../utils/ble-utils.js'
import ConnectPanel from '../../components/connect-panel/connect-panel.vue'

export default {
  components: {
    ConnectPanel
  },

  data() {
    return {
      connState: 'idle',
      connError: '',
      deviceId: '',
      displayName: DEVICE_DISPLAY_NAME,
      temp: '--',
      humidity: '--',
      isPowerOn: false,
      protocolList: PROTOCOL_LIST,
      protocolIndex: 0,
      targetTemp: 26,
      modeList: MODE_LIST,
      modeIndex: 0,
      speedList: SPEED_LIST,
      speedIndex: 0,
      status: '',
      statusType: '',
      bluetoothReady: false,
      isLearning: false,
      deviceList: [],
      serviceUUID: BLE_CONFIG.serviceUUID,
      rxCharUUID: BLE_CONFIG.rxCharUUID,
      txCharUUID: BLE_CONFIG.txCharUUID,
      targetServiceId: '',
      rxCharId: '',
      txCharId: '',
      showProtocolPicker: false,
      showModePicker: false,
      showSpeedPicker: false,
      isLoading: false,
      loadingText: '加载中...',
      commandQueue: [],
      isSending: false,
      connectionAttempts: 0,
      _connectingTo: '',
      _listenersBound: false,
      _deviceFoundBound: false,
      maxConnectionAttempts: 3,
      reconnectTimer: null,
      commandRetryCount: 0,
      maxCommandRetries: 3,
      lastCommandTime: 0,
      commandInterval: 800,
      savedDeviceId: '',
      savedDeviceName: '',
      useSavedDevice: true,
      reconnectAttempts: 0,
      maxReconnectAttempts: 3,
      connectTimeout: 15000,
      connectionSucceeded: false,
      timers: {},
      maxQueueLength: 20,
      dataBuffer: '',
      bufferTimer: null
    }
  },

  computed: {
    isConnected() {
      return this.connState === 'connected'
    },
    savedDevice() {
      if (!this.savedDeviceId) return null
      return {
        deviceId: this.savedDeviceId,
        name: this.savedDeviceName || this.savedDeviceId
      }
    },
    connPillClass() {
      if (this.connState === 'connected') return 'connected'
      if (this.connState === 'scanning' || this.connState === 'connecting' || this.connState === 'reconnecting') {
        return 'connecting'
      }
      return 'disconnected'
    },
    connPillText() {
      switch (this.connState) {
        case 'connected':
          return '● 已连接'
        case 'scanning':
          return '○ 扫描中'
        case 'connecting':
          return '○ 连接中'
        case 'reconnecting':
          return '○ 重连中'
        default:
          return '○ 未连接'
      }
    },
    currentDeviceName() {
      if (!this.deviceId) return ''
      const found = this.deviceList.find((d) => d.deviceId === this.deviceId)
      return (found && (found.name || found.deviceId)) || this.savedDeviceName || ''
    },
    tempRingStyle() {
      const t = parseFloat(this.temp)
      let pct = 0
      if (!isNaN(t)) {
        pct = Math.min(100, Math.max(0, ((t - 17) / (30 - 17)) * 100))
      }
      return {
        background: `conic-gradient(#2563eb ${pct}%, #e2e8f0 ${pct}% 100%)`
      }
    },
    humPercent() {
      const h = parseFloat(this.humidity)
      return isNaN(h) ? 0 : Math.min(100, Math.max(0, h))
    }
  },

  onLoad() {
    const savedDeviceId = uni.getStorageSync('savedDeviceId')
    const savedDeviceName = uni.getStorageSync('savedDeviceName')
    if (savedDeviceId) {
      this.savedDeviceId = savedDeviceId
      this.savedDeviceName = savedDeviceName || ''
      console.log('从本地存储获取到保存的设备ID:', savedDeviceId)
    }
    this.showLoading('初始化蓝牙...')
    this.initBluetooth()
  },

  onUnload() {
    Object.keys(this.timers).forEach((type) => this.clearTimer(type))
    if (this.bufferTimer) {
      clearTimeout(this.bufferTimer)
      this.bufferTimer = null
    }

    try {
      if (typeof uni.offBluetoothAdapterStateChange === 'function') uni.offBluetoothAdapterStateChange()
      if (typeof uni.offBLEConnectionStateChange === 'function') uni.offBLEConnectionStateChange()
      if (typeof uni.offBLECharacteristicValueChange === 'function') uni.offBLECharacteristicValueChange()
      if (typeof uni.offBluetoothDeviceFound === 'function') uni.offBluetoothDeviceFound()
    } catch (e) {
      console.warn('清理监听失败:', e)
    }

    this._listenersBound = false
    this._deviceFoundBound = false
    this.clearReconnectTimer()
    this.disconnectBLE()

    uni.closeBluetoothAdapter({
      fail: (err) => console.warn('关闭蓝牙适配器失败:', err)
    })
  },

  methods: {
    async initBluetooth() {
      await ensureNativePermissions()
      uni.getBluetoothAdapterState({
        success: (res) => {
          if (res.available) {
            this.bluetoothReady = true
            this.initBLEListeners()
            this.showStatus('蓝牙已开启', 'success')
            this.hideLoading()
            this.tryAutoConnect()
          } else {
            this.openBluetoothAdapter()
          }
        },
        fail: () => this.openBluetoothAdapter()
      })
    },

    tryAutoConnect() {
      const savedDeviceId = this.savedDeviceId
      if (savedDeviceId && !this.isConnected) {
        console.log('尝试自动连接已保存的设备:', savedDeviceId)
        this.connState = 'connecting'
        this.connError = ''
        setTimeout(() => {
          if (this.connState === 'connecting') {
            this.connectBLE(savedDeviceId, { silent: true })
          }
        }, 1000)
      }
    },

    openBluetoothAdapter() {
      uni.openBluetoothAdapter({
        success: () => {
          this.bluetoothReady = true
          this.initBLEListeners()
          this.showStatus('蓝牙初始化成功', 'success')
          this.hideLoading()
          this.tryAutoConnect()
        },
        fail: (err) => {
          this.bluetoothReady = false
          this.connState = 'idle'
          this.connError = '蓝牙初始化失败，请确认手机蓝牙已开启'
          this.showStatus('蓝牙初始化失败', 'fail')
          if (err.errCode === 10001) {
            uni.showModal({
              title: '提示',
              content: '请打开手机蓝牙后重试',
              showCancel: false
            })
          }
          this.hideLoading()
        }
      })
    },

    initBLEListeners() {
      if (this._listenersBound) return
      this._listenersBound = true

      uni.onBluetoothAdapterStateChange((res) => {
        this.bluetoothReady = res.available
        if (!res.available) {
          if (this.isConnected) this.disconnectBLE()
          uni.stopBluetoothDevicesDiscovery({
            fail: () => {}
          })
          this.connState = 'idle'
          this.connError = '手机蓝牙已关闭，请重新开启'
          this.showStatus('蓝牙已关闭', 'fail')
        } else {
          this.showStatus('蓝牙已开启', 'success')
          if (this.deviceId && !this.isConnected && this.connState === 'idle') {
            this.scheduleReconnect()
          }
        }
      })

      uni.onBLEConnectionStateChange((res) => {
        console.log('蓝牙连接状态变化:', res)
        if (!res.connected) {
          const wasConnected = this.connState === 'connected'
          const wasReconnecting = this.connState === 'reconnecting'
          const wasConnecting = this.connState === 'connecting'
          this.targetServiceId = ''
          this.rxCharId = ''
          this.txCharId = ''
          this.showStatus('设备已断开连接', 'fail')
          this.syncGlobalBle()
          if (wasConnected) {
            this.connState = 'idle'
            this.scheduleReconnect()
          } else if (!wasReconnecting && !wasConnecting) {
            this.connState = 'idle'
            this.connError = '设备连接已断开'
          }
          // 重连中/连接中时保持原状态，让重连流程继续
        }
      })

      uni.onBLECharacteristicValueChange((res) => {
        console.log('接收到特征值变化:', res)
        if (res.characteristicId === this.txCharId) {
          this.processData(res.value)
        }
      })
    },

    async startDeviceDiscovery() {
      this.showStatus('正在搜索设备...', '')
      this.deviceList = []

      // 先停掉上一次可能还在进行的扫描
      uni.stopBluetoothDevicesDiscovery({
        fail: () => {}
      })

      // 设备发现监听只在页面生命周期内注册一次（App 端没有 offBluetoothDeviceFound）
      if (!this._deviceFoundBound && typeof uni.onBluetoothDeviceFound === 'function') {
        uni.onBluetoothDeviceFound((res) => {
          res.devices.forEach((device) => {
            if (!device.advertisServiceUUIDs || device.advertisServiceUUIDs.length === 0) return
            const matched = device.advertisServiceUUIDs.some(
              (uuid) => normalizeUUID(uuid) === normalizeUUID(this.serviceUUID)
            )
            if (matched && device.name) {
              if (!this.deviceList.some((d) => d.deviceId === device.deviceId)) {
                this.deviceList.push({
                  deviceId: device.deviceId,
                  name: device.name || '未知设备',
                  RSSI: device.RSSI
                })
                this.deviceList.sort((a, b) => (b.RSSI || -100) - (a.RSSI || -100))
                this.showStatus(`找到目标设备: ${device.name}`, 'success')
              }
            }
          })
        })
        this._deviceFoundBound = true
      }

      await ensureNativePermissions()
      uni.startBluetoothDevicesDiscovery({
        services: [this.serviceUUID],
        allowDuplicatesKey: false,
        success: () => {
          this.createTimer('search', 8000, () => {
            uni.stopBluetoothDevicesDiscovery()
            if (this.deviceList.length === 0) {
              this.connState = 'idle'
              this.connError = '未找到设备，请确认设备已开机并处于可发现状态'
              this.showStatus('未找到设备，请确认设备已开机并处于可发现状态', 'fail')
              this.hideLoading()
            } else {
              this.showStatus(`找到 ${this.deviceList.length} 个设备`, 'success')
              this.hideLoading()
            }
          })
        },
        fail: (err) => {
          console.error('搜索设备失败:', err)
          let errorMsg = '搜索设备失败'
          switch (err.errCode) {
            case 10000:
              errorMsg = '蓝牙适配器未初始化'
              break
            case 10001:
              errorMsg = '蓝牙适配器不可用，请开启蓝牙'
              break
            case 10002:
              errorMsg = '设备未找到，请确保设备处于可发现状态'
              break
            default:
              errorMsg = '搜索设备失败: ' + err.errMsg
          }
          this.connState = 'idle'
          this.connError = errorMsg
          this.showStatus(errorMsg, 'fail')
          this.hideLoading()
        }
      })
    },

    beginScan() {
      if (!this.bluetoothReady) {
        this.connState = 'idle'
        this.connError = '蓝牙未就绪，正在重新初始化…'
        this.showStatus('蓝牙未就绪，请稍候', '')
        this.initBluetooth()
        return
      }
      this.connError = ''
      this.connState = 'scanning'
      this.startDeviceDiscovery()
    },

    stopScan() {
      uni.stopBluetoothDevicesDiscovery({
        fail: (err) => console.warn('停止扫描失败:', err)
      })
      this.connState = 'idle'
      this.connError = ''
      this.hideLoading()
    },

    connectDevice(deviceId) {
      if (!deviceId) return
      this.connectionAttempts = 0
      this.reconnectAttempts = 0
      this.clearReconnectTimer()
      this.connState = 'connecting'
      this.connError = ''
      this.connectBLE(deviceId)
    },

    connectBLE(deviceId, opts = {}) {
      if (!deviceId) {
        this.showStatus('请选择设备', 'fail')
        return
      }
      if (this._connectingTo === deviceId) {
        console.log('已有进行中的连接，忽略重复请求:', deviceId)
        return
      }
      this._connectingTo = deviceId

      uni.stopBluetoothDevicesDiscovery({
        success: () => console.log('已停止设备扫描，准备连接'),
        fail: (err) => console.warn('停止扫描失败:', err)
      })

      this.connectionAttempts += 1
      this.connectionSucceeded = false

      if (this.connectionAttempts > this.maxConnectionAttempts) {
        this._connectingTo = ''
        this.connState = 'idle'
        this.connError = '连接失败，请稍后重试'
        this.showStatus('连接尝试次数过多，请稍后再试', 'fail')
        this.hideLoading()
        return
      }

      uni.createBLEConnection({
        deviceId,
        timeout: this.connectTimeout,
        success: () => {
          this.connectionSucceeded = true
          this._connectingTo = ''
          this.connState = 'connected'
          this.connError = ''
          this.reconnectAttempts = 0
          this.deviceId = deviceId
          this.connectionAttempts = 0
          this.clearReconnectTimer()
          this.showStatus('设备连接成功', 'success')

          if (deviceId !== this.savedDeviceId) {
            this.savedDeviceId = deviceId
            uni.setStorage({ key: 'savedDeviceId', data: deviceId })
          }
          const found = this.deviceList.find((d) => d.deviceId === deviceId)
          const name = (found && found.name) || this.savedDeviceName || deviceId
          this.savedDeviceName = name
          uni.setStorage({ key: 'savedDeviceName', data: name })

          this.syncGlobalBle()
          this.getDeviceServices(deviceId)
        },
        fail: (err) => {
          this._connectingTo = ''
          console.error('连接失败:', err)
          let errorMsg = '连接失败'
          switch (err.errCode) {
            case 10003:
              errorMsg = '连接失败，请确保设备距离较近且未被其他设备连接'
              break
            case 10012:
              errorMsg = '连接超时，请稍后重试'
              break
            case 10013:
              errorMsg = '设备ID无效，请重新搜索设备'
              break
            default:
              errorMsg = `连接失败: ${err.errMsg}`
          }
          this.connError = errorMsg
          this.showStatus(errorMsg, 'fail')

          if (opts.silent) {
            // 自动重连：本次失败交给 scheduleReconnect 的下一轮（带递增等待和适配器重置）
            this.hideLoading()
            this.connState = 'reconnecting'
            this.scheduleReconnect()
            return
          }

          if (this.connectionAttempts < this.maxConnectionAttempts) {
            setTimeout(() => {
              if (this.connState === 'connecting') {
                this.connectBLE(deviceId, opts)
              }
            }, 2000)
          } else {
            // 清零次数，保证之后手动连接不会被“次数过多”挡住
            this.connectionAttempts = 0
            this.connState = 'idle'
            this.connError = errorMsg + '，请重试'
            this.hideLoading()
          }
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
        fail: (err) => {
          this.showStatus('获取服务失败: ' + err.errMsg, 'fail')
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
              this.enableNotification(deviceId, serviceId, char.uuid)
            }
          }

          if (!rxCharId || !txCharId) {
            this.showStatus('未找到全部所需特征', 'fail')
            this.hideLoading()
          } else {
            this.rxCharId = rxCharId
            this.txCharId = txCharId
            this.showStatus('蓝牙初始化完成', 'success')
            this.syncGlobalBle()
            setTimeout(() => this.loadDeviceStatus(), 800)
            this.hideLoading()
          }
        },
        fail: (err) => {
          this.showStatus('获取特征失败: ' + err.errMsg, 'fail')
          this.hideLoading()
        }
      })
    },

    enableNotification(deviceId, serviceId, characteristicId) {
      const enable = (retry = 0) => {
        uni.notifyBLECharacteristicValueChange({
          deviceId,
          serviceId,
          characteristicId,
          state: true,
          success: () => console.log('启用通知成功'),
          fail: (err) => {
            console.error('启用通知失败:', err)
            if (retry < 2) {
              setTimeout(() => enable(retry + 1), 1000)
            } else {
              this.showStatus('启用通知失败，请重新连接', 'fail')
            }
          }
        })
      }
      enable()
    },

    queryCurrentProtocol() {
      if (!this.isConnected) return
      this.enqueueCommand('get_protocol', this.rxCharId)
      console.log('主动查询设备当前协议')
    },

    // 协议名加中文注释显示，如 KELVINATOR（凯利文特）
    protocolLabel(name) {
      if (!name) return ''
      const cn = PROTOCOL_CN[name]
      return cn ? `${name}（${cn}）` : name
    },

    loadDeviceStatus() {
      this.enqueueCommand('status', this.rxCharId)
      setTimeout(() => {
        this.enqueueCommand('power', this.rxCharId)
        setTimeout(() => {
          this.enqueueCommand('get_protocol', this.rxCharId)
        }, 300)
      }, 300)
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
        /^(switching=wifi|already=wifi_mode)$/.test(buf) ||
        /^temp=[\d.]+;mode=\d+;speed=\d+;power=(on|off)$/.test(buf) ||
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
        this.dataBuffer = ''
      }, ms)
    },

    handleMessage(data) {
      try {
        if (data.startsWith('t')) {
          const tempMatch = data.match(/t([\d.]+)h([\d.]+)/)
          if (tempMatch) {
            this.temp = tempMatch[1]
            this.humidity = tempMatch[2]
            return
          }
        }

        const timeInfo = data.match(/^time=(.+)$/)
        if (timeInfo) {
          console.log('设备时间:', timeInfo[1])
          return
        }

        const tempMatch = data.match(/temp=([\d.]+)/)
        const humidityMatch = data.match(/humidity=([\d.]+)/)
        if (tempMatch && humidityMatch) {
          this.temp = tempMatch[1]
          this.humidity = humidityMatch[1]

          const protocolMatchInStatus = data.match(/protocol=(\w+)/)
          if (protocolMatchInStatus) {
            const idx = this.protocolList.indexOf(protocolMatchInStatus[1])
            if (idx !== -1) this.protocolIndex = idx
          }
          const powerMatch = data.match(/power=(on|off)/)
          if (powerMatch) this.isPowerOn = powerMatch[1] === 'on'
          return
        }

        const protocolMatch = data.match(/^protocol=(\w+)$/)
        if (protocolMatch) {
          const protocol = protocolMatch[1]
          if (protocol === 'invalid') {
            this.showStatus('协议设置失败：无效的协议名称', 'fail')
          } else {
            const idx = this.protocolList.indexOf(protocol)
            if (idx !== -1) {
              this.protocolIndex = idx
              if (!this.isLoading) this.showStatus(`当前协议已更新为: ${this.protocolLabel(protocol)}`, 'success')
              else console.log('协议已更新为:', protocol)
            } else {
              this.showStatus(`未知协议: ${protocol}`, 'fail')
            }
          }
          return
        }

        const learnMatch = data.match(/learn=(\w+)(:(\w+))?/)
        if (learnMatch) {
          const status = learnMatch[1]
          if (status === 'success' && learnMatch[3]) {
            this.isLearning = false
            const idx = this.protocolList.indexOf(learnMatch[3])
            if (idx !== -1) this.protocolIndex = idx
            this.showStatus(`学习成功！已识别协议: ${this.protocolLabel(learnMatch[3])}`, 'success')
          } else if (status === 'timeout') {
            this.isLearning = false
            this.showStatus('学习超时，请重试', 'fail')
          }
          return
        }

        const powerMatch = data.match(/power=(on|off)/)
        if (powerMatch) {
          this.isPowerOn = powerMatch[1] === 'on'
          this.showStatus(`空调${powerMatch[1] === 'on' ? '已开启' : '已关闭'}`, 'success')
          return
        }

        if (data) console.log('收到数据:', data)
      } catch (error) {
        console.error('数据解析错误:', error)
        this.showStatus('数据解析失败', 'fail')
      }
    },

    disconnectBLE() {
      this.clearReconnectTimer()
      this._connectingTo = ''
      if (this.isConnected && this.deviceId) {
        uni.closeBLEConnection({
          deviceId: this.deviceId,
          success: () => {
            this.connState = 'idle'
            this.connError = ''
            this.deviceId = ''
            this.targetServiceId = ''
            this.rxCharId = ''
            this.txCharId = ''
            this.showStatus('已断开连接', 'success')
            this.syncGlobalBle()
          },
          fail: (err) => this.showStatus('断开连接失败: ' + err.errMsg, 'fail')
        })
      }
    },

    togglePower() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      const newPowerState = !this.isPowerOn
      this.isPowerOn = newPowerState
      if (newPowerState) {
        this.targetTemp = 25
        this.modeIndex = 0
        this.speedIndex = 0
        this.sendSettings()
      } else {
        const command = 'power=off'
        this.enqueueCommand(command, this.rxCharId)
        this.showStatus('正在关闭空调...', '')
      }
    },

    queryDeviceStatus() {
      if (!this.isConnected) return
      this.enqueueCommand('status', this.rxCharId)
    },

    startLearningMode() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      this.isLearning = true
      this.showStatus('请按下空调遥控器任意按钮...', '')
      this.enqueueCommand('learn=start', this.rxCharId)

      this.createTimer('learn', 12000, () => {
        if (this.isLearning) {
          this.isLearning = false
          this.showStatus('学习超时，请重试', 'fail')
        }
      })
    },

    openProtocolPicker() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      this.showProtocolPicker = true
    },
    hideProtocolPicker() {
      this.showProtocolPicker = false
    },
    openModePicker() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      this.showModePicker = true
    },
    hideModePicker() {
      this.showModePicker = false
    },
    openSpeedPicker() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      this.showSpeedPicker = true
    },
    hideSpeedPicker() {
      this.showSpeedPicker = false
    },

    stopReconnect() {
      this.clearReconnectTimer()
      this._connectingTo = ''
      this.reconnectAttempts = 0
      this.connectionAttempts = 0
      if (this.deviceId) {
        uni.closeBLEConnection({
          deviceId: this.deviceId,
          fail: (err) => console.warn('关闭连接失败:', err)
        })
      }
      uni.stopBluetoothDevicesDiscovery({
        fail: () => {}
      })
      this.connState = 'idle'
      this.connError = ''
      this.syncGlobalBle()
      this.showStatus('已取消重连', '')
    },

    forgetDevice() {
      uni.removeStorageSync('savedDeviceId')
      uni.removeStorageSync('savedDeviceName')
      this.savedDeviceId = ''
      this.savedDeviceName = ''
      this.connError = ''
      this.showStatus('已忘记该设备', 'success')
    },

    openSettings() {
      try {
        uni.openAppAuthorizeSetting({
          fail: () => this.showStatus('请到系统设置中开启蓝牙和定位权限', '')
        })
      } catch (e) {
        uni.showModal({
          title: '权限设置',
          content: '请到系统设置中，为「时欢智能空调助手」开启蓝牙和定位权限。',
          showCancel: false
        })
      }
    },

    handleRetry() {
      this.connError = ''
      this.initBluetooth()
    },

    syncGlobalBle() {
      getApp().globalData.ble = {
        connected: this.connState === 'connected',
        deviceId: this.deviceId,
        serviceId: this.targetServiceId,
        rxCharId: this.rxCharId,
        txCharId: this.txCharId
      }
    },

    onProtocolChange(index) {
      this.protocolIndex = index
      this.showProtocolPicker = false
      if (this.isConnected) {
        const protocol = this.protocolList[index]
        this.enqueueCommand(`protocol=${protocol}`, this.rxCharId)
        this.showStatus(`已设置协议: ${this.protocolLabel(protocol)}`, 'success')
      }
    },

    onModeChange(index) {
      this.modeIndex = index
      this.showModePicker = false
      if (this.isConnected) this.sendSettings()
    },

    onSpeedChange(index) {
      this.speedIndex = index
      this.showSpeedPicker = false
      if (this.isConnected) this.sendSettings()
    },

    increaseTemp() {
      if (this.targetTemp < 30) {
        this.targetTemp += 1
        if (this.isConnected) this.sendSettings()
      } else {
        this.showStatus('温度已达最大值30℃', '')
      }
    },

    decreaseTemp() {
      if (this.targetTemp > 17) {
        this.targetTemp -= 1
        if (this.isConnected) this.sendSettings()
      } else {
        this.showStatus('温度已达最小值17℃', '')
      }
    },

    sendSettings() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      if (!this.isPowerOn) return

      const command = `temp=${this.targetTemp};mode=${this.modeIndex};speed=${this.speedIndex};power=on`
      this.enqueueCommand(command, this.rxCharId)
      const modeText = this.modeList[this.modeIndex]
      const speedText = this.speedList[this.speedIndex]
      this.showStatus(`已发送: ${this.targetTemp}℃ ${modeText} ${speedText}`, 'success')
    },

    enqueueCommand(command, characteristicId) {
      if (this.commandQueue.length >= this.maxQueueLength) {
        this.showStatus('命令队列已满，请稍后再试', 'fail')
        this.commandQueue.shift()
      }
      this.commandQueue.push({
        command,
        characteristicId,
        timestamp: Date.now(),
        retries: 0
      })
      if (!this.isSending) this.processCommandQueue()
    },

    processCommandQueue() {
      if (this.commandQueue.length === 0) {
        this.isSending = false
        return
      }

      const now = Date.now()
      if (now - this.lastCommandTime < this.commandInterval) {
        setTimeout(
          () => this.processCommandQueue(),
          this.commandInterval - (now - this.lastCommandTime)
        )
        return
      }

      const commandItem = this.commandQueue[0]
      if (!this.isConnected || !this.targetServiceId || !commandItem.characteristicId) {
        this.showStatus('未连接设备或特征ID无效', 'fail')
        this.commandQueue.shift()
        this.processCommandQueue()
        return
      }

      this.isSending = true
      this.lastCommandTime = now
      console.log('准备发送数据:', commandItem.command)

      enqueueWrite(() => new Promise((resolve) => {
        uni.writeBLECharacteristicValue({
          deviceId: this.deviceId,
          serviceId: this.targetServiceId,
          characteristicId: commandItem.characteristicId,
          value: stringToArrayBuffer(commandItem.command),
          success: () => {
            console.log('数据发送成功:', commandItem.command)
            this.commandQueue.shift()
            this.processCommandQueue()
            resolve()
          },
          fail: (err) => {
            console.error('蓝牙写入失败:', err)
            commandItem.retries++
            if (commandItem.retries >= this.maxCommandRetries) {
              // 写入连续失败说明连接可能已异常，不再重试，交给重连逻辑处理
              this.commandQueue = []
              this.isSending = false
              this.showStatus('命令发送失败，正在尝试重连…', 'fail')
              if (this.connState === 'connected') this.scheduleReconnect()
              resolve()
              return
            }
            this.processCommandQueue()
            resolve()
          }
        })
      }))
    },

    showStatus(message, type) {
      this.status = message
      this.statusType = type || ''
      const timeout = type === 'fail' ? 5000 : 3000
      setTimeout(() => {
        if (this.status === message) this.status = ''
      }, timeout)
    },

    showLoading(text) {
      this.isLoading = true
      this.loadingText = text
    },

    hideLoading() {
      this.isLoading = false
    },

    goToTimer() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      uni.navigateTo({ url: '/pages/timer/timer' })
    },

    goHelp() {
      uni.navigateTo({
        url: '/pages/help/help',
        fail: (err) => {
          console.error('跳转更多信息失败:', err)
          this.showStatus('页面打开失败，请重新运行 App 后再试', 'fail')
        }
      })
    },

    factoryReset() {
      if (!this.isConnected) {
        this.showStatus('请先连接设备', 'fail')
        return
      }
      uni.showModal({
        title: '恢复出厂设置',
        content: '将清除 HomeKit 配对、WiFi 配置和所有定时任务，设备会重启。确定继续吗？',
        confirmText: '确定重置',
        confirmColor: '#dc2626',
        success: (res) => {
          if (res.confirm) {
            this.enqueueCommand('reset_factory', this.rxCharId)
            this.showStatus('已发送重置指令，设备即将重启', '')
          }
        }
      })
    },

    scheduleReconnect() {
      this.clearReconnectTimer()
      if (this.reconnectAttempts >= this.maxReconnectAttempts) {
        this.reconnectAttempts = 0
        this.connectionAttempts = 0
        this.connState = 'idle'
        this.connError = '重连失败，请手动重新连接'
        this.showStatus('重连失败，请手动重新连接', 'fail')
        return
      }
      this.reconnectAttempts += 1
      this.connectionAttempts = 0
      this.connState = 'reconnecting'
      this.connError = ''
      const delay = 3000 * this.reconnectAttempts // 3s / 6s / 9s 递增
      this.reconnectTimer = setTimeout(async () => {
        if (!this.deviceId || this.connState !== 'reconnecting') return
        this.showStatus('尝试重新连接...', '')
        this.connState = 'connecting'
        // Android 连续失败后重置蓝牙适配器，能明显提高重连成功率
        if (this.reconnectAttempts >= 2) {
          await this.resetAdapter()
        }
        await this.ensureAdapterReady()
        if (this.connState === 'connecting' && this.deviceId) {
          this.connectBLE(this.deviceId, { silent: true })
        }
      }, delay)
    },

    resetAdapter() {
      return new Promise((resolve) => {
        uni.closeBluetoothAdapter({
          complete: () => {
            setTimeout(() => {
              uni.openBluetoothAdapter({
                success: () => {
                  this.bluetoothReady = true
                  this.initBLEListeners()
                  resolve()
                },
                fail: () => {
                  this.bluetoothReady = false
                  this.connState = 'idle'
                  this.connError = '蓝牙不可用，请开启手机蓝牙后重试'
                  resolve()
                }
              })
            }, 300)
          }
        })
      })
    },

    ensureAdapterReady() {
      return new Promise((resolve) => {
        uni.getBluetoothAdapterState({
          success: (res) => {
            if (res.available) {
              resolve()
            } else {
              this.openAdapterAndResolve(resolve)
            }
          },
          fail: () => this.openAdapterAndResolve(resolve)
        })
      })
    },

    openAdapterAndResolve(resolve) {
      uni.openBluetoothAdapter({
        success: () => {
          this.bluetoothReady = true
          this.initBLEListeners()
          resolve()
        },
        fail: () => {
          this.bluetoothReady = false
          this.connState = 'idle'
          this.connError = '蓝牙不可用，请开启手机蓝牙后重试'
          this.showStatus('蓝牙不可用，请开启手机蓝牙', 'fail')
          resolve()
        }
      })
    },

    clearReconnectTimer() {
      if (this.reconnectTimer) {
        clearTimeout(this.reconnectTimer)
        this.reconnectTimer = null
      }
    },

    createTimer(type, delay, callback) {
      this.clearTimer(type)
      const timer = setTimeout(() => {
        callback()
        this.clearTimer(type)
      }, delay)
      this.timers[type] = timer
    },

    clearTimer(type) {
      if (this.timers[type]) {
        clearTimeout(this.timers[type])
        delete this.timers[type]
      }
    },

    noop() {}
  }
}
</script>

<style>
.device-selector,
.picker {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 24rpx 28rpx;
  background: #ffffff;
  border-radius: 18rpx;
  box-shadow: 0 8rpx 24rpx rgba(15, 23, 42, 0.05);
}

.picker-value {
  font-size: 28rpx;
  color: #1f2937;
}

.picker-placeholder {
  font-size: 28rpx;
  color: #9ca3af;
}

.picker-arrow {
  color: #9ca3af;
  font-size: 24rpx;
}

.ble-control-group {
  margin: 24rpx 0;
}

.ble-button,
.power-button,
.timer-button,
.action-button {
  width: 100%;
  border-radius: 22rpx;
  font-size: 30rpx;
  font-weight: 600;
  line-height: 2.4;
}

.ble-button {
  color: #ffffff;
  background: #3a7bd5;
}

.ble-button.connected-btn {
  background: #ef4444;
}

.sensor-display {
  display: flex;
  gap: 20rpx;
  margin-bottom: 24rpx;
}

.sensor-item {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6rpx;
  padding: 30rpx 16rpx;
  background: #ffffff;
  border-radius: 22rpx;
  box-shadow: 0 10rpx 28rpx rgba(37, 99, 235, 0.08);
  border: 1rpx solid rgba(255, 255, 255, 0.7);
}

.sensor-item.temp {
  background: linear-gradient(145deg, #f3f8ff 0%, #ffffff 70%);
}

.sensor-item.hum {
  background: linear-gradient(145deg, #f0fbf7 0%, #ffffff 70%);
}

.sensor-icon {
  font-size: 44rpx;
}

.sensor-value {
  font-size: 40rpx;
  font-weight: 700;
  color: #111827;
}

.sensor-label {
  font-size: 24rpx;
  color: #6b7280;
}

.power-button {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 14rpx;
  color: #ffffff;
  margin-bottom: 28rpx;
}

.power-button.power-on {
  background: linear-gradient(135deg, #22c55e, #16a34a);
  box-shadow: 0 12rpx 28rpx rgba(22, 163, 74, 0.28);
}

.power-button.power-off {
  background: linear-gradient(135deg, #94a3b8, #64748b);
  box-shadow: 0 12rpx 28rpx rgba(100, 116, 139, 0.25);
}

.power-icon {
  font-size: 34rpx;
}

.section-title {
  position: relative;
  padding-left: 20rpx;
  font-size: 30rpx;
  font-weight: 700;
  color: #111827;
  margin: 8rpx 0 18rpx;
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

.settings-card {
  padding: 8rpx 24rpx;
  background: #ffffff;
  border-radius: 24rpx;
  box-shadow: 0 12rpx 32rpx rgba(37, 99, 235, 0.08);
  margin-bottom: 24rpx;
}

.form-group {
  padding: 22rpx 0;
  border-bottom: 1rpx solid #f1f5f9;
}

.form-group:last-child {
  border-bottom: none;
}

.form-label {
  display: block;
  font-size: 24rpx;
  color: #6b7280;
  margin-bottom: 14rpx;
}

.form-control-row {
  display: flex;
  align-items: center;
  gap: 16rpx;
}

.form-control-row .picker {
  flex: 1;
}

.refresh-btn {
  font-size: 36rpx;
  color: #3a7bd5;
  padding: 0 8rpx;
}

.refresh-btn.spinning {
  animation: spin 0.8s linear infinite;
}

.temp-control {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.temp-button {
  width: 88rpx;
  height: 72rpx;
  line-height: 72rpx;
  text-align: center;
  border-radius: 16rpx;
  background: #f1f5f9;
  color: #111827;
  font-size: 36rpx;
}

.temp-value {
  font-size: 44rpx;
  font-weight: 700;
  color: #111827;
}

.buttons-group {
  display: flex;
  gap: 20rpx;
  margin-bottom: 24rpx;
}

.action-button {
  flex: 1;
  color: #ffffff;
}

.learning-button {
  background: linear-gradient(135deg, #fbbf24, #f59e0b);
  box-shadow: 0 10rpx 24rpx rgba(245, 158, 11, 0.28);
}

.update-button {
  background: linear-gradient(135deg, #6ba6f5, #3a7bd5);
  box-shadow: 0 10rpx 24rpx rgba(58, 123, 213, 0.28);
}

.timer-button {
  color: #2563eb;
  background: linear-gradient(135deg, #ffffff, #e8f1ff);
  border: 1rpx solid rgba(58, 123, 213, 0.25);
}

.factory-reset-btn {
  margin-top: 18rpx;
  color: #dc2626;
  background: #fef2f2;
  border-radius: 22rpx;
  font-size: 28rpx;
  line-height: 2.4;
}

/* ===== 方案一：精致蓝白（覆盖式优化） ===== */
.subtitle {
  display: block;
  margin-top: 6rpx;
  font-size: 22rpx;
  color: #6b7280;
  font-weight: 400;
}

.sensor-item {
  border-radius: 26rpx;
  padding: 30rpx 12rpx;
  background: rgba(255, 255, 255, 0.86);
  box-shadow: 0 12rpx 30rpx rgba(37, 99, 235, 0.08);
  border: 1rpx solid rgba(255, 255, 255, 0.9);
}

.sensor-item.temp {
  background: linear-gradient(150deg, #f3f8ff, #ffffff 72%);
}

.sensor-item.hum {
  background: linear-gradient(150deg, #eefaf8, #ffffff 72%);
}

.temp-ring {
  width: 178rpx;
  height: 178rpx;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 10rpx;
}

.temp-inner {
  width: 150rpx;
  height: 150rpx;
  border-radius: 50%;
  background: #ffffff;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: inset 0 4rpx 14rpx rgba(37, 99, 235, 0.06);
}

.sensor-item .sensor-value {
  font-size: 38rpx;
}

.hum-bar {
  width: 150rpx;
  height: 12rpx;
  border-radius: 999rpx;
  background: #e2e8f0;
  overflow: hidden;
  margin-top: 8rpx;
}

.hum-fill {
  height: 100%;
  border-radius: 999rpx;
  background: linear-gradient(90deg, #22d3ee, #0ea5e9);
}

.power-wrap {
  display: flex;
  flex-direction: column;
  align-items: center;
  margin: 26rpx 0 30rpx;
}

.power-circle {
  width: 220rpx;
  height: 220rpx;
  border-radius: 50%;
  border: none;
  display: flex;
  align-items: center;
  justify-content: center;
}

.power-circle .power-icon {
  font-size: 74rpx;
}

.power-circle.power-on {
  background: radial-gradient(circle at 35% 30%, #34d399, #059669 70%);
  box-shadow: 0 18rpx 46rpx rgba(5, 150, 105, 0.34);
}

.power-circle.power-off {
  background: radial-gradient(circle at 35% 30%, #94a3b8, #64748b 70%);
  box-shadow: 0 18rpx 46rpx rgba(100, 116, 139, 0.26);
}

.power-circle[disabled] {
  opacity: 0.55;
}

.power-label {
  margin-top: 18rpx;
  font-size: 28rpx;
  font-weight: 600;
  color: #334155;
}

.section-title {
  color: #172033;
}

.section-title::before {
  background: linear-gradient(180deg, #2563eb, #22d3ee);
}

.settings-card {
  border-radius: 28rpx;
  background: rgba(255, 255, 255, 0.88);
  box-shadow: 0 14rpx 36rpx rgba(37, 99, 235, 0.08);
}

.form-group {
  padding: 26rpx 0;
}

.form-label {
  color: #64748b;
}

.picker {
  border-radius: 18rpx;
  background: #f4f8ff;
}

.refresh-btn {
  color: #2563eb;
}

.action-button {
  border-radius: 24rpx;
}

.learning-button {
  background: linear-gradient(135deg, #fbbf24, #f59e0b);
  box-shadow: 0 12rpx 26rpx rgba(245, 158, 11, 0.24);
}

.update-button {
  background: linear-gradient(135deg, #38bdf8, #2563eb);
  box-shadow: 0 12rpx 26rpx rgba(37, 99, 235, 0.24);
}

.timer-button {
  color: #2563eb;
  background: linear-gradient(135deg, #ffffff, #e8f2ff);
  border: 1rpx solid rgba(37, 99, 235, 0.2);
  border-radius: 24rpx;
}

.factory-reset-btn {
  border-radius: 24rpx;
}

/* ===== premium-ui-builder-skill：层级、状态、动效 ===== */
.settings-summary {
  display: flex;
  gap: 16rpx;
  margin-bottom: 28rpx;
}

.summary-chip {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6rpx;
  padding: 22rpx 8rpx;
  border-radius: var(--r-md);
  background: var(--c-surface);
  border: 1rpx solid var(--c-border);
  box-shadow: var(--shadow-card);
}

.summary-label {
  font-size: 22rpx;
  color: var(--c-muted);
}

.summary-value {
  font-size: 30rpx;
  font-weight: 700;
  color: var(--c-text);
}

.section-title {
  margin: 8rpx 0 20rpx;
}

.picker:active {
  background: #e8f0ff;
}

.power-circle:active {
  transform: scale(0.96);
}

.action-button:active,
.timer-button:active,
.factory-reset-btn:active {
  transform: scale(0.97);
  opacity: 0.92;
}

.action-button[disabled],
.timer-button[disabled],
.factory-reset-btn[disabled],
.power-circle[disabled] {
  opacity: 0.45;
}

.power-circle {
  transition: transform 0.12s ease-out;
}

.action-button,
.timer-button,
.factory-reset-btn,
.picker {
  transition: transform 0.12s ease-out, background 0.15s ease-out;
}

@media (prefers-reduced-motion: reduce) {
  .power-circle:active,
  .action-button:active,
  .timer-button:active,
  .factory-reset-btn:active {
    transform: none;
  }
}

/* ===== HomeKit 风格：紧凑控制面板 ===== */
.container {
  padding: 24rpx 24rpx calc(140rpx + env(safe-area-inset-bottom));
}

.title {
  font-size: 46rpx;
}

.subtitle {
  display: block;
  margin-top: 6rpx;
  font-size: 24rpx;
  color: var(--c-muted);
  font-weight: 400;
}

.connected-bar {
  display: flex;
  align-items: center;
  gap: 12rpx;
  padding: 18rpx 22rpx;
  border-radius: var(--r-md);
  background: var(--c-surface);
  border: 1rpx solid var(--c-border);
  box-shadow: var(--shadow-card);
}

.connected-dot {
  width: 14rpx;
  height: 14rpx;
  border-radius: 50%;
  background: var(--c-success);
}

.connected-text {
  flex: 1;
  font-size: 26rpx;
  font-weight: 600;
  color: var(--c-text);
}

.connected-action {
  font-size: 24rpx;
  color: var(--c-error);
}

.ambient-row {
  display: flex;
  justify-content: center;
  gap: 34rpx;
  padding: 22rpx 0 26rpx;
}

.ambient-item {
  font-size: 28rpx;
  font-weight: 500;
  color: var(--c-muted);
}

.ambient-item.link {
  color: var(--c-primary);
}

.control-card {
  background: var(--c-surface);
  border-radius: var(--r-lg);
  border: 1rpx solid var(--c-border);
  box-shadow: var(--shadow-card);
  padding: 14rpx 30rpx;
}

.power-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 28rpx 0;
}

.control-label {
  display: block;
  font-size: 24rpx;
  color: var(--c-muted);
}

.power-state {
  display: block;
  margin-top: 4rpx;
  font-size: 26rpx;
  font-weight: 600;
}

.power-state.on {
  color: var(--c-success);
}

.power-state.off {
  color: var(--c-muted);
}

.switch {
  width: 96rpx;
  height: 56rpx;
  border-radius: 999rpx;
  background: #e4e4e7;
  padding: 4rpx;
  transition: background 0.2s ease-out;
}

.switch.on {
  background: #34c759;
}

.switch-knob {
  width: 48rpx;
  height: 48rpx;
  border-radius: 50%;
  background: #ffffff;
  box-shadow: 0 2rpx 6rpx rgba(0, 0, 0, 0.18);
  transition: transform 0.2s ease-out;
}

.switch.on .switch-knob {
  transform: translateX(40rpx);
}

.divider {
  height: 1rpx;
  background: var(--c-border);
}

.temp-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 30rpx 8rpx;
}

.temp-display {
  display: flex;
  align-items: baseline;
}

.temp-num {
  font-size: 108rpx;
  font-weight: 300;
  line-height: 1;
  color: var(--c-text);
}

.temp-unit {
  font-size: 40rpx;
  font-weight: 400;
  color: var(--c-muted);
  margin-left: 6rpx;
}

.step-btn {
  width: 100rpx;
  height: 100rpx;
  border-radius: 50%;
  background: #f0f0f3;
  color: var(--c-text);
  font-size: 48rpx;
  line-height: 100rpx;
  padding: 0;
  text-align: center;
}

.step-btn:active {
  background: #e2e2e7;
}

.step-btn[disabled] {
  opacity: 0.4;
}

.seg-block {
  padding: 24rpx 0;
}

.seg-block .control-label {
  margin-bottom: 14rpx;
}

.seg {
  display: flex;
  background: #ececf1;
  border-radius: 14rpx;
  padding: 6rpx;
}

.seg-item {
  flex: 1;
  text-align: center;
  font-size: 26rpx;
  color: var(--c-muted);
  padding: 16rpx 0;
  border-radius: 10rpx;
  transition: background 0.15s ease-out, color 0.15s ease-out;
}

.seg-item.active {
  background: #ffffff;
  color: var(--c-text);
  font-weight: 600;
  box-shadow: 0 2rpx 8rpx rgba(0, 0, 0, 0.08);
}

.protocol-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 28rpx 0;
}

.protocol-value {
  display: flex;
  align-items: center;
  gap: 8rpx;
  font-size: 28rpx;
  font-weight: 600;
  color: var(--c-text);
}

.chevron {
  font-size: 32rpx;
  color: var(--c-muted);
}

.actions-row {
  display: flex;
  justify-content: center;
  gap: 16rpx;
  margin-top: 24rpx;
}

.mini-btn {
  flex: 0 0 auto;
  padding: 0 36rpx;
  font-size: 28rpx;
  color: var(--c-text);
  background: var(--c-surface);
  border: 1rpx solid var(--c-border);
  border-radius: 999rpx;
  line-height: 2.6;
  transition: transform 0.12s ease-out, opacity 0.15s ease-out;
}

.mini-btn.danger {
  color: var(--c-error);
}

.mini-btn.learn {
  color: #ffffff;
  border: none;
  background: linear-gradient(135deg, #fbbf24, #f59e0b);
  box-shadow: 0 10rpx 24rpx rgba(245, 158, 11, 0.26);
}

.mini-btn.timer {
  color: #ffffff;
  border: none;
  background: linear-gradient(135deg, #38bdf8, #2563eb);
  box-shadow: 0 10rpx 24rpx rgba(37, 99, 235, 0.26);
}

.mini-btn.danger {
  color: #dc2626;
  background: #fef2f2;
  border: none;
}

.mini-btn:active {
  transform: scale(0.97);
}

.mini-btn[disabled] {
  opacity: 0.4;
}

.footer-area {
  margin-top: 36rpx;
  display: flex;
  flex-direction: column;
  align-items: center;
}

.more-info {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 4rpx;
  font-size: 22rpx;
  color: #9ca3af;
  padding: 20rpx 40rpx;
  border-radius: 999rpx;
  min-width: 220rpx;
}

.more-info-hover {
  background: rgba(148, 163, 184, 0.12);
}

@media (prefers-reduced-motion: reduce) {
  .switch-knob,
  .switch,
  .seg-item {
    transition: none;
  }
  .mini-btn:active,
  .step-btn:active {
    transform: none;
  }
}

/* 底部固定状态区：设置信息始终显示在最下面，不会被遮挡 */
.status-bar {
  position: fixed;
  left: 0;
  right: 0;
  bottom: 0;
  height: calc(88rpx + env(safe-area-inset-bottom));
  padding-bottom: env(safe-area-inset-bottom);
  display: flex;
  align-items: center;
  justify-content: center;
  background: transparent; /* 融入页面背景，不突兀 */
  z-index: 1000;
  pointer-events: none; /* 透明状态条不拦截底部任何点击 */
}

.status-text {
  max-width: 90%;
  font-size: 26rpx;
  color: var(--c-text);
  text-align: center;
  pointer-events: auto;
}

.status-text.success {
  color: var(--c-success);
}

.status-text.fail {
  color: var(--c-error);
}
</style>
