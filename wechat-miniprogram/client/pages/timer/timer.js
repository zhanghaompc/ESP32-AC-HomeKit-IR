// 定时任务页面
const MODE_LIST = ["自动", "制冷", "制热", "除湿", "送风"];
const SPEED_LIST = ["自动", "低速", "中速", "强劲"];

// BLE核心配置
const BLE_CONFIG = {
  serviceUUID: '6E400001-B5A3-F393-E0A9-E50E24DCCA9E',
  rxCharUUID: '6E400002-B5A3-F393-E0A9-E50E24DCCA9E',
  txCharUUID: '6E400003-B5A3-F393-E0A9-E50E24DCCA9E'
};

Page({
  data: {
    // BLE连接状态
    isConnected: false,
    deviceId: '',
    serviceUUID: BLE_CONFIG.serviceUUID,
    rxCharUUID: BLE_CONFIG.rxCharUUID,
    txCharUUID: BLE_CONFIG.txCharUUID,
    targetServiceId: '',
    rxCharId: '',
    txCharId: '',
    
    // 设备时间
    currentTime: '',
    currentDate: '',
    
    // 定时任务列表
    timerList: [],
    modeList: MODE_LIST,
    speedList: SPEED_LIST,
    
    // 添加任务弹窗
    showAddModal: false,
    showModeSelector: false,
    showSpeedSelector: false,
    hourList: Array.from({length: 24}, (_, i) => i + '时'),
    minuteList: Array.from({length: 60}, (_, i) => i + '分'),
    newTimer: {
      hour: 8,
      minute: 0,
      temp: 25,
      mode: 0,
      speed: 0,
      power: 'on',
      repeat: true
    },
    
    // 编辑状态
    editingTimer: false,
    currentEditingId: null,
    
    // 状态
    isLoading: false,
    loadingText: '加载中...',
    status: '',
    statusType: '',
    
    // 命令队列
    commandQueue: [],
    isSending: false,
    lastCommandTime: 0,
    commandInterval: 800,
    maxQueueLength: 20,
    maxCommandRetries: 3,
    
    // 编辑任务临时数据
    pendingEditTimer: null,
    
    // 定时器
    timeRefreshTimer: null
  },

  onLoad() {
    // 从本地存储获取设备ID
    const savedDeviceId = wx.getStorageSync('savedDeviceId');
    if (savedDeviceId) {
      this.setData({ deviceId: savedDeviceId });
      // 尝试连接设备
      this.connectBLE(savedDeviceId);
    } else {
      this.showStatus('请先返回首页连接设备', 'fail');
    }
  },

  onUnload() {
    // 清除时间刷新定时器
    this.clearTimeRefreshTimer();
    
    // 断开连接
    if (this.data.isConnected) {
      wx.closeBLEConnection({
        deviceId: this.data.deviceId,
        fail: (err) => console.warn('断开连接失败:', err)
      });
    }
    // 清理监听
    wx.offBLEConnectionStateChange();
    wx.offBLECharacteristicValueChange();
  },

  // ========== BLE连接管理 ==========
  
  connectBLE(deviceId) {
    this.showLoading('正在连接设备...');
    
    wx.createBLEConnection({
      deviceId: deviceId,
      timeout: 15000,
      success: (res) => {
        this.setData({ 
          isConnected: true,
          deviceId: deviceId
        });
        this.showStatus('设备连接成功', 'success');
        this.getDeviceServices(deviceId);
      },
      fail: (err) => {
        this.showStatus('连接失败: ' + err.errMsg, 'fail');
        this.hideLoading();
      }
    });
  },

  getDeviceServices(deviceId) {
    wx.getBLEDeviceServices({
      deviceId: deviceId,
      success: (res) => {
        let targetService = null;
        for (let i = 0; i < res.services.length; i++) {
          const service = res.services[i];
          if (this.normalizeUUID(service.uuid) === this.normalizeUUID(this.data.serviceUUID)) {
            targetService = service;
            break;
          }
        }
        
        if (targetService) {
          this.setData({ targetServiceId: targetService.uuid });
          this.getDeviceCharacteristics(deviceId, targetService.uuid);
        } else {
          this.showStatus('未找到目标服务', 'fail');
          this.hideLoading();
        }
      },
      fail: (err) => {
        this.showStatus('获取服务失败', 'fail');
        this.hideLoading();
      }
    });
  },

  getDeviceCharacteristics(deviceId, serviceId) {
    wx.getBLEDeviceCharacteristics({
      deviceId: deviceId,
      serviceId: serviceId,
      success: (res) => {
        let rxCharId = '';
        let txCharId = '';
        
        for (let i = 0; i < res.characteristics.length; i++) {
          const char = res.characteristics[i];
          if (this.normalizeUUID(char.uuid) === this.normalizeUUID(this.data.rxCharUUID)) {
            rxCharId = char.uuid;
          } else if (this.normalizeUUID(char.uuid) === this.normalizeUUID(this.data.txCharUUID)) {
            txCharId = char.uuid;
          }
        }
        
        if (rxCharId && txCharId) {
          this.setData({ rxCharId: rxCharId, txCharId: txCharId });
          // 先设置特征ID，再启用通知
          this.enableNotification(deviceId, serviceId, txCharId);
          this.showStatus('蓝牙初始化完成', 'success');
          // 初始化数据
          setTimeout(() => {
            this.refreshTime();
            this.refreshTimerList();
            // 启动自动刷新时间的定时器（每5秒）
            this.startTimeRefreshTimer();
          }, 500);
          this.hideLoading();
        } else {
          this.showStatus('未找到全部所需特征', 'fail');
          this.hideLoading();
        }
      },
      fail: (err) => {
        this.showStatus('获取特征失败', 'fail');
        this.hideLoading();
      }
    });
  },

  enableNotification(deviceId, serviceId, characteristicId) {
    wx.notifyBLECharacteristicValueChange({
      deviceId: deviceId,
      serviceId: serviceId,
      characteristicId: characteristicId,
      state: true,
      success: () => console.log('启用通知成功'),
      fail: (err) => console.error('启用通知失败:', err)
    });
    
    // 监听特征值变化
    wx.onBLECharacteristicValueChange((res) => {
      if (res.characteristicId === this.data.txCharId) {
        this.processData(res.value);
      }
    });
  },

  // ========== 数据处理 ==========
  
  processData(buffer) {
    try {
      const data = String.fromCharCode.apply(null, new Uint8Array(buffer));
      console.log('[BLE接收] 解析数据:', data);
      
      // 1. 解析时间响应
      const timeMatch = data.match(/time=(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})/);
      if (timeMatch) {
        const timeStr = timeMatch[1];
        const parts = timeStr.split(' ');
        this.setData({
          currentDate: parts[0],
          currentTime: parts[1]
        });
        console.log('时间数据已更新:', parts[0], parts[1]);
        return;
      }
      
      // 2. 解析定时任务列表
      const timerListMatch = data.match(/timers=(.+)/);
      if (timerListMatch) {
        try {
          const timers = JSON.parse(timerListMatch[1]);
          this.setData({ timerList: timers });
          this.showStatus('定时任务列表已更新', 'success');
        } catch (e) {
          console.error('解析定时任务列表失败:', e);
          this.showStatus('解析任务列表失败', 'fail');
        }
        this.hideLoading();
        return;
      }
      
      // 3. 解析添加任务响应
      const addMatch = data.match(/timer_add=(\w+);id=(\d+)/);
      if (addMatch) {
        if (addMatch[1] === 'success') {
          // 检查是否是更新操作
          if (this.data.editingTimer) {
            this.showStatus('定时任务更新成功', 'success');
          } else {
            this.showStatus('定时任务添加成功', 'success');
          }
          // 添加或更新后退出弹窗
          this.hideAddModal();
          this.refreshTimerList();
        } else {
          this.showStatus('添加任务失败', 'fail');
        }
        this.hideLoading();
        return;
      }
      

      
      // 4. 解析删除任务响应
      const deleteMatch = data.match(/timer_delete=(\w+);id=(\d+)/);
      if (deleteMatch) {
        if (deleteMatch[1] === 'success') {
          // 检查是否有待编辑的任务
          if (this.data.pendingEditTimer) {
            const timer = this.data.pendingEditTimer;
            let addCommand;
            if (timer.power === 'on') {
              addCommand = `timer=add;hour=${timer.hour};minute=${timer.minute};temp=${timer.temp};mode=${timer.mode};speed=${timer.speed};power=${timer.power};repeat=${timer.repeat ? 1 : 0}`;
            } else {
              addCommand = `timer=add;hour=${timer.hour};minute=${timer.minute};power=${timer.power};repeat=${timer.repeat ? 1 : 0}`;
            }
            // 发送添加命令
            this.sendCommand(addCommand);
            // 清除待编辑任务
            this.setData({ pendingEditTimer: null });
          } else {
            this.showStatus('定时任务删除成功', 'success');
            this.refreshTimerList();
            this.hideLoading();
          }
        } else {
          this.showStatus('删除任务失败', 'fail');
          this.hideLoading();
        }
        return;
      }
      
      // 5. 解析启用/禁用任务响应
      const enableMatch = data.match(/timer_enable=(\w+);id=(\d+)/);
      if (enableMatch) {
        if (enableMatch[1] === 'success') {
          this.showStatus('任务状态已更新', 'success');
          this.refreshTimerList();
        } else {
          this.showStatus('更新任务状态失败', 'fail');
        }
        this.hideLoading();
        return;
      }
      
    } catch (error) {
      console.error('数据解析错误:', error);
    }
  },

  // ========== 命令发送 ==========
  
  sendCommand(command) {
    if (!this.data.isConnected || !this.data.targetServiceId || !this.data.rxCharId) {
      this.showStatus('设备未连接', 'fail');
      return;
    }
    
    // 检查命令间隔
    const now = Date.now();
    if (now - this.data.lastCommandTime < this.data.commandInterval) {
      setTimeout(() => this.sendCommand(command), this.data.commandInterval - (now - this.data.lastCommandTime));
      return;
    }
    
    this.setData({ lastCommandTime: now });
    
    wx.writeBLECharacteristicValue({
      deviceId: this.data.deviceId,
      serviceId: this.data.targetServiceId,
      characteristicId: this.data.rxCharId,
      value: this.stringToArrayBuffer(command),
      success: () => {
        console.log('命令发送成功:', command);
      },
      fail: (err) => {
        console.error('命令发送失败:', err);
        this.showStatus('发送失败', 'fail');
      }
    });
  },

  stringToArrayBuffer(str) {
    const buffer = new ArrayBuffer(str.length);
    const dataView = new DataView(buffer);
    for (let i = 0; i < str.length; i++) {
      dataView.setUint8(i, str.charCodeAt(i));
    }
    return buffer;
  },

  normalizeUUID(uuid) {
    if (!uuid) return '';
    const clean = uuid.replace(/-/g, '').toUpperCase();
    if (clean.length === 4) {
      return `0000${clean}-0000-1000-8000-00805F9B34FB`.toUpperCase();
    }
    if (clean.length === 32) {
      return `${clean.substring(0,8)}-${clean.substring(8,12)}-${clean.substring(12,16)}-${clean.substring(16,20)}-${clean.substring(20)}`.toUpperCase();
    }
    return clean;
  },

  // ========== 时间相关 ==========
  
  refreshTime() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    this.sendCommand('time');
  },



  // ========== 定时任务管理 ==========
  
  refreshTimerList() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    this.showLoading('加载任务列表...');
    this.sendCommand('timer=list');
    // 设置超时
    setTimeout(() => {
      if (this.data.isLoading) {
        this.hideLoading();
        this.showStatus('加载超时', 'fail');
      }
    }, 5000);
  },

  showAddModal() {
    this.setData({
      showAddModal: true,
      editingTimer: false,
      currentEditingId: null,
      newTimer: {
        hour: 8,
        minute: 0,
        temp: 25,
        mode: 0,
        speed: 0,
        power: 'on',
        repeat: true
      }
    });
  },

  // 编辑定时任务
  editTimer(e) {
    const item = e.currentTarget.dataset.item;
    this.setData({
      showAddModal: true,
      editingTimer: true,
      currentEditingId: item.id,
      newTimer: {
        hour: item.hour,
        minute: item.minute,
        temp: item.temp || 25,
        mode: item.mode || 0,
        speed: item.speed || 0,
        power: item.power,
        repeat: item.repeat
      }
    });
  },

  hideAddModal() {
    this.setData({ 
      showAddModal: false,
      editingTimer: false,
      currentEditingId: null
    });
  },

  addTimer() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    
    const timer = this.data.newTimer;
    let command;
    
    if (timer.power === 'on') {
      command = `timer=add;hour=${timer.hour};minute=${timer.minute};temp=${timer.temp};mode=${timer.mode};speed=${timer.speed};power=${timer.power};repeat=${timer.repeat ? 1 : 0}`;
    } else {
      command = `timer=add;hour=${timer.hour};minute=${timer.minute};power=${timer.power};repeat=${timer.repeat ? 1 : 0}`;
    }
    
    this.showLoading('添加任务...');
    this.sendCommand(command);
  },

  // 更新定时任务 (先删除后添加)
  updateTimer() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    
    const timer = this.data.newTimer;
    const id = this.data.currentEditingId;
    
    // 保存待编辑的任务数据
    this.setData({ pendingEditTimer: timer });
    
    // 先删除旧任务
    this.showLoading('更新任务...');
    this.sendCommand(`timer=delete;id=${id}`);
  },

  deleteTimer(e) {
    const id = e.currentTarget.dataset.id;
    wx.showModal({
      title: '确认删除',
      content: '确定要删除这个定时任务吗？',
      success: (res) => {
        if (res.confirm) {
          this.showLoading('删除任务...');
          this.sendCommand(`timer=delete;id=${id}`);
        }
      }
    });
  },

  // 启用/禁用定时任务
  toggleTimerEnable(e) {
    const id = e.currentTarget.dataset.id;
    const enabled = e.detail.value;
    const state = enabled ? 1 : 0;
    this.sendCommand(`timer=enable;id=${id};state=${state}`);
  },

  // 切换到WiFi模式
  toggleWifiMode() {
    this.showStatus('切换到WiFi模式...', '');
    
    // 发送WiFi模式切换命令（非阻塞）
    if (this.data.isConnected && this.data.targetServiceId && this.data.rxCharId) {
      wx.writeBLECharacteristicValue({
        deviceId: this.data.deviceId,
        serviceId: this.data.targetServiceId,
        characteristicId: this.data.rxCharId,
        value: this.stringToArrayBuffer('wifi_mode'),
        success: () => {
          console.log('命令发送成功: wifi_mode');
        },
        fail: (err) => {
          console.error('命令发送失败:', err);
        }
      });
    }
    
    // 直接退出小程序（在用户点击事件中调用）
    console.log('准备退出小程序');
    wx.exitMiniProgram({
      success: function(res) {
        console.log('小程序退出成功', res);
      },
      fail: function(err) {
        console.log('小程序退出失败', err);
      }
    });
  },

  // 显示模式选择器
  showModeSelector() {
    this.setData({ showModeSelector: true });
  },

  // 隐藏模式选择器
  hideModeSelector() {
    this.setData({ showModeSelector: false });
  },

  // 选择模式
  selectMode(e) {
    const index = parseInt(e.currentTarget.dataset.index);
    this.setData({ 
      'newTimer.mode': index,
      showModeSelector: false
    });
  },

  // 显示风速选择器
  showSpeedSelector() {
    this.setData({ showSpeedSelector: true });
  },

  // 隐藏风速选择器
  hideSpeedSelector() {
    this.setData({ showSpeedSelector: false });
  },

  // 选择风速
  selectSpeed(e) {
    const index = parseInt(e.currentTarget.dataset.index);
    this.setData({ 
      'newTimer.speed': index,
      showSpeedSelector: false
    });
  },

  // 保存定时任务（添加或更新）
  saveTimer() {
    if (this.data.editingTimer) {
      this.updateTimer();
    } else {
      this.addTimer();
    }
  },

  // 启动时间刷新定时器
  startTimeRefreshTimer() {
    // 先清除已有的定时器
    this.clearTimeRefreshTimer();
    
    // 每5秒刷新一次时间
    const timer = setInterval(() => {
      if (this.data.isConnected) {
        this.refreshTime();
      } else {
        this.clearTimeRefreshTimer();
      }
    }, 5000);
    
    this.setData({ timeRefreshTimer: timer });
  },

  // 清除时间刷新定时器
  clearTimeRefreshTimer() {
    if (this.data.timeRefreshTimer) {
      clearInterval(this.data.timeRefreshTimer);
      this.setData({ timeRefreshTimer: null });
    }
  },

  // ========== 添加任务表单处理 ==========
  
  onHourChange(e) {
    const hour = parseInt(e.detail.value);
    this.setData({ 'newTimer.hour': hour });
  },

  onMinuteChange(e) {
    const minute = parseInt(e.detail.value);
    this.setData({ 'newTimer.minute': minute });
  },

  onRepeatChange(e) {
    this.setData({ 'newTimer.repeat': e.detail.value });
  },

  onPowerChange(e) {
    this.setData({ 'newTimer.power': e.detail.value });
  },

// 温度变化
  onTempChange(e) {
    const temp = parseInt(e.detail.value);
    this.setData({ 'newTimer.temp': temp });
  },

  // 增加温度
  increaseTemp() {
    let temp = this.data.newTimer.temp;
    if (temp < 30) {
      temp++;
      this.setData({ 'newTimer.temp': temp });
    }
  },

  // 减少温度
  decreaseTemp() {
    let temp = this.data.newTimer.temp;
    if (temp > 16) {
      temp--;
      this.setData({ 'newTimer.temp': temp });
    }
  },

  setPowerOn() {
    this.setData({ 'newTimer.power': 'on' });
  },

  setPowerOff() {
    this.setData({ 'newTimer.power': 'off' });
  },

  noop() {},

  // ========== 工具方法 ==========
  
  showLoading(text) {
    this.setData({ isLoading: true, loadingText: text });
  },

  hideLoading() {
    this.setData({ isLoading: false });
  },

  showStatus(message, type) {
    this.setData({ status: message, statusType: type });
    setTimeout(() => {
      this.setData({ status: '' });
    }, type === 'fail' ? 5000 : 3000);
  }
});