// 抽离常量，提升可维护性
const PROTOCOL_LIST = [
  "AC", "AMCOR", "AIRWELL", "AIRTON", "ARGO", "ARRIS", "BOSE", "BOSCH144",
  "CARRIER_AC", "CARRIER_AC40", "CARRIER_AC64", "CARRIER_AC84", "CARRIER_AC128",
  "CLIMABUTLER", "COOLIX", "COOLIX48", "CORONA_AC", "DAIKIN", "DAIKIN2", "DAIKIN64",
  "DAIKIN128", "DAIKIN152", "DAIKIN160", "DAIKIN176", "DAIKIN200", "DAIKIN216", "DAIKIN312",
  "DENON", "DELONGHI_AC", "DISH", "DOSHISHA", "ELECTRA_AC", "EPSON", "ECOCLIM",
  "ELITESCREENS", "GICABLE", "GLOBALCACHE", "GREE", "GOODWEATHER", "GORENJE",
  "HAIER_AC", "HAIER_AC160", "HAIER_AC176", "HAIER_AC_YRW02", "HITACHI_AC",
  "HITACHI_AC1", "HITACHI_AC2", "HITACHI_AC3", "HITACHI_AC264", "HITACHI_AC296",
  "HITACHI_AC344", "HITACHI_AC424", "INAX", "JVC", "KELON", "KELON168", "KELVINATOR",
  "LASERTAG", "LEGOPF", "LG", "LG2", "LUTRON", "MAGIQUEST", "MIDEA", "MIDEA24",
  "METZ", "MIRAGE", "MITSUBISHI", "MITSUBISHI2", "MITSUBISHI112", "MITSUBISHI136",
  "MITSUBISHI_AC", "MITSUBISHI_HEAVY_88", "MITSUBISHI_HEAVY_152", "MULTIBRACKETS",
  "MWM", "NEC", "NEC_LIKE", "NEOCLIMA", "NIKAI", "PANASONIC", "PANASONIC_AC",
  "PANASONIC_AC32", "PIONEER", "PRONTO", "RAW", "RC5", "RC5X", "RC6", "RCMM",
  "RHOSS", "SAMSUNG", "SAMSUNG36", "SAMSUNG_AC", "SANYO", "SANYO_AC", "SANYO_AC88",
  "SANYO_AC152", "SANYO_LC7461", "SAR", "SHARP", "SHARP_AC", "SHERWOOD", "SONY",
  "SONY_38K", "SYMPHONY", "TECHNIBEL_AC", "TECO", "TEKNOPOINT", "TCL96AC", "TCL112AC",
  "TOSHIBA_AC", "TOTO", "TRANSCOLD", "TRUMA", "TROTEC", "TROTEC_3550", "UNKNOWN",
  "UNUSED", "VESTEL_AC", "VOLTAS", "WHIRLPOOL_AC", "WHYNTER", "WOWWEE", "XMP",
  "YORK", "ZEPEAL"
];

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
    isConnected: false,
    deviceId: '',
    temp: "--",
    humidity: "--",
    isPowerOn: false,
    protocolList: PROTOCOL_LIST,
    protocolIndex: 0,
    targetTemp: 26,
    tempQuickList: [
      { temp: 22 },
      { temp: 24 },
      { temp: 26 },
      { temp: 28 },
      { temp: 30 }
    ],
    modeList: MODE_LIST,
    modeIndex: 0,
    speedList: SPEED_LIST,
    speedIndex: 0,
    currentDeviceName: '',
    deviceRSSI: '',
    status: "",
    statusType: "",
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
    showDevicePicker: false,
    isLoading: false,
    loadingText: '加载中...',
    commandQueue: [],
    isSending: false,
    connectionAttempts: 0,
    maxConnectionAttempts: 3,
    reconnectTimer: null,
    commandRetryCount: 0,
    maxCommandRetries: 3,
    lastCommandTime: 0,
    commandInterval: 800,
    savedDeviceId: '',
    useSavedDevice: true,
    connectTimeout: 15000,
    // 优化新增
    connectionSucceeded: false, // 修复作用域问题
    timers: {}, // 统一定时器管理
    maxQueueLength: 20, // 命令队列最大长度
    dataBuffer: '' // 用于存储跨数据包的数据
  },

  onLoad() {
    // 从本地存储获取保存的设备ID
    const savedDeviceId = wx.getStorageSync('savedDeviceId');
    if (savedDeviceId) {
      this.setData({ savedDeviceId: savedDeviceId });
      console.log('从本地存储获取到保存的设备ID:', savedDeviceId);
    }
    
    // 初始化蓝牙
    this.showLoading('初始化蓝牙...');
    this.initBluetooth();
  },

  onUnload() {
    // 清理所有定时器
    const timers = this.data.timers;
    for (let type in timers) {
      this.clearTimer(type);
    }
    // 清理蓝牙相关监听
    wx.offBluetoothAdapterStateChange();
    wx.offBLEConnectionStateChange();
    wx.offBLECharacteristicValueChange();
    wx.offBluetoothDeviceFound();
    // 清理重连定时器
    this.clearReconnectTimer();
    // 断开蓝牙连接
    this.disconnectBLE();
    // 关闭蓝牙适配器
    wx.closeBluetoothAdapter({
      fail: (err) => console.warn('关闭蓝牙适配器失败:', err)
    });
  },

  // 初始化蓝牙适配器
  initBluetooth() {
    // 请求蓝牙和位置权限（安卓必需）
    wx.getSetting({
      success: (res) => {
        // 检查蓝牙权限
        if (!res.authSetting['scope.bluetooth']) {
          wx.authorize({
            scope: 'scope.bluetooth',
            success: () => this.checkLocationPermission(res),
            fail: (err) => {
              console.error('蓝牙权限获取失败:', err);
              this.showStatus('请授予蓝牙权限以使用此功能', 'fail');
              this.hideLoading();
            }
          });
        } else {
          this.checkLocationPermission(res);
        }
      },
      fail: (err) => {
        console.error('获取设置失败:', err);
        this.showStatus('初始化失败，请重试', 'fail');
        this.hideLoading();
      }
    });
  },

  // 检查位置权限（安卓搜索BLE需要）
  checkLocationPermission(settings) {
    // 替换废弃的wx.getSystemInfoSync，使用新API
    let platform = 'unknown';
    try {
      // 优先使用新API
      const deviceInfo = wx.getDeviceInfo();
      platform = deviceInfo.platform;
    } catch (e) {
      // 兼容旧版本
      const systemInfo = wx.getSystemInfoSync();
      platform = systemInfo.platform;
    }

    if (platform === 'android') {
      // 检查定位权限
      if (!settings.authSetting['scope.userLocation']) {
        wx.authorize({
          scope: 'scope.userLocation',
          success: () => {
            console.log('位置权限已获取');
            // 检查定位开关是否打开
            this.checkLocationEnabled();
          },
          fail: () => {
            wx.showModal({
              title: '权限提示',
              content: '安卓设备搜索蓝牙需要位置权限，请在设置中开启',
              confirmText: '去设置',
              success: (res) => {
                if (res.confirm) {
                  wx.openSetting({
                    success: (res) => {
                      if (res.authSetting['scope.userLocation']) {
                        this.checkLocationEnabled();
                      } else {
                        this.showStatus('请开启位置权限以搜索蓝牙设备', 'fail');
                        this.hideLoading();
                      }
                    }
                  });
                }
              }
            });
            this.hideLoading();
          }
        });
      } else {
        // 检查定位开关是否打开
        this.checkLocationEnabled();
      }
    } else {
      this.setupBluetooth();
    }
  },

  // 检查定位开关是否打开
  checkLocationEnabled() {
    wx.getLocation({
      type: 'wgs84',
      success: () => {
        this.setupBluetooth();
      },
      fail: () => {
        wx.showModal({
          title: '提示',
          content: '请打开手机定位开关以搜索蓝牙设备',
          showCancel: false
        });
        this.hideLoading();
      }
    });
  },

  // 优化：先检查蓝牙状态再初始化
  setupBluetooth() {
    // 先检查蓝牙适配器状态
    wx.getBluetoothAdapterState({
      success: (res) => {
        if (res.available) {
          this.setData({ bluetoothReady: true });
          this.initBLEListeners();
          this.showStatus('蓝牙已开启', 'success');
          this.hideLoading();
          // 蓝牙就绪后，如果有保存的设备ID，自动尝试连接
          this.tryAutoConnect();
        } else {
          this.openBluetoothAdapter();
        }
      },
      fail: () => this.openBluetoothAdapter()
    });
  },

  // 尝试自动连接已保存的设备
  tryAutoConnect() {
    const savedDeviceId = this.data.savedDeviceId;
    if (savedDeviceId && !this.data.isConnected) {
      console.log('尝试自动连接已保存的设备:', savedDeviceId);
      this.showLoading('正在自动连接设备...');
      // 延迟一点再连接，确保蓝牙完全就绪
      setTimeout(() => {
        this.connectBLE(savedDeviceId);
      }, 1000);
    }
  },

  // 打开蓝牙适配器
  openBluetoothAdapter() {
    wx.openBluetoothAdapter({
      success: (res) => {
        this.setData({ bluetoothReady: true });
        this.initBLEListeners();
        this.showStatus('蓝牙初始化成功', 'success');
        this.hideLoading();
        // 蓝牙初始化成功后，尝试自动连接
        this.tryAutoConnect();
      },
      fail: (err) => {
        this.setData({ bluetoothReady: false });
        this.showStatus('蓝牙初始化失败: ' + err.errMsg, 'fail');
        
        if (err.errCode === 10001) {
          wx.showModal({
            title: '提示',
            content: '请打开手机蓝牙后重试',
            showCancel: false
          });
        }
        this.hideLoading();
      }
    });
  },

  // 初始化蓝牙监听事件
  initBLEListeners() {
    // 监听蓝牙适配器状态变化
    wx.onBluetoothAdapterStateChange((res) => {
      this.setData({ bluetoothReady: res.available });
      
      if (!res.available) {
        if (this.data.isConnected) {
          this.disconnectBLE();
        }
        this.showStatus('蓝牙已关闭', 'fail');
      } else {
        this.showStatus('蓝牙已开启', 'success');
        if (this.data.deviceId) {
          this.scheduleReconnect();
        }
      }
    });
    
    // 监听蓝牙连接状态变化
    wx.onBLEConnectionStateChange((res) => {
      console.log('蓝牙连接状态变化:', res);
      if (!res.connected) {
        this.setData({
          isConnected: false,
          currentDeviceName: '',
          deviceRSSI: '',
          targetServiceId: '',
          rxCharId: '',
          txCharId: ''
        });
        this.showStatus('设备已断开连接', 'fail');
        this.scheduleReconnect();
      }
    });
    
    // 监听特征值变化（接收数据）
    wx.onBLECharacteristicValueChange((res) => {
      console.log('接收到特征值变化:', res);
      if (res.characteristicId === this.data.txCharId) {
        this.processData(res.value);
      }
    });
  },

  // 开始搜索设备
  startDeviceDiscovery() {
    this.showStatus('正在搜索设备...', '');
    this.setData({ deviceList: [] });
    
    // 监听发现新设备事件
    wx.onBluetoothDeviceFound((res) => {
      res.devices.forEach((device) => {
        // 过滤掉没有服务UUID的设备
        if (!device.advertisServiceUUIDs || device.advertisServiceUUIDs.length === 0) return;
        
        // 检查设备是否包含目标服务UUID（兼容短UUID）
        if (device.advertisServiceUUIDs.some(uuid => 
          this.normalizeUUID(uuid) === this.normalizeUUID(this.data.serviceUUID))) {
          
          // 放宽设备名称过滤条件，只要有名称就添加
          if (device.name) {
            const deviceList = this.data.deviceList;
            // 避免重复添加
            if (!deviceList.some(d => d.deviceId === device.deviceId)) {
              const newDevice = {
                deviceId: device.deviceId,
                name: device.name || '未知设备',
                RSSI: device.RSSI
              };
              deviceList.push(newDevice);
              this.setData({ deviceList: deviceList });
              this.showStatus(`找到目标设备: ${device.name}`, 'success');
            }
          }
        }
      });
    });
    
    wx.startBluetoothDevicesDiscovery({
      services: [this.data.serviceUUID],
      allowDuplicatesKey: false,
      success: () => {
        // 设置搜索超时（8秒）- 使用统一定时器管理
        this.createTimer('search', 8000, () => {
          wx.stopBluetoothDevicesDiscovery();
          
          if (this.data.deviceList.length === 0) {
            this.showStatus('未找到设备，请确认设备已开机并处于可发现状态', 'fail');
            // 提供手动重新搜索的选项
            wx.showModal({
              title: '提示',
              content: '未找到设备，是否重新搜索？',
              success: (res) => {
                if (res.confirm) {
                  this.startDeviceDiscovery();
                } else {
                  this.hideLoading();
                }
              }
            });
          } else {
            this.showStatus(`找到 ${this.data.deviceList.length} 个设备`, 'success');
            this.hideLoading();
            // 自动连接第一个设备
            if (this.data.deviceList.length > 0 && !this.data.isConnected) {
              this.connectBLE(this.data.deviceList[0].deviceId);
            }
          }
        });
      },
      fail: (err) => {
        console.error('搜索设备失败:', err);
        let errorMsg = '搜索设备失败';
        switch (err.errCode) {
          case 10000:
            errorMsg = '蓝牙适配器未初始化';
            break;
          case 10001:
            errorMsg = '蓝牙适配器不可用，请开启蓝牙';
            break;
          case 10002:
            errorMsg = '设备未找到，请确保设备处于可发现状态';
            break;
          default:
            errorMsg = '搜索设备失败: ' + err.errMsg;
        }
        this.showStatus(errorMsg, 'fail');
        this.hideLoading();
      }
    });
  },

  // 手动触发连接设备
  toggleConnection() {
    if (this.data.isConnected) {
      this.disconnectBLE();
    } else {
      this.manualConnect();
    }
  },

  manualConnect() {
    if (!this.data.bluetoothReady) {
      this.showStatus('蓝牙未就绪，请稍后再试', 'fail');
      return;
    }
    
    if (this.data.deviceList.length === 0) {
      this.showLoading('正在搜索设备...');
      this.startDeviceDiscovery();
      return;
    }
    
    // 连接第一个设备
    this.connectBLE(this.data.deviceList[0].deviceId);
  },

  // 连接到指定设备
  connectBLE(deviceId) {
    if (!deviceId) {
      this.showStatus('请选择设备', 'fail');
      return;
    }
    
    // 停止设备扫描
    wx.stopBluetoothDevicesDiscovery({
      success: () => console.log('已停止设备扫描，准备连接'),
      fail: (err) => console.warn('停止扫描失败:', err)
    });
    
    // 替换Object.assign，避免展开运算符
    const newData = Object.assign({}, this.data, {
      connectionAttempts: this.data.connectionAttempts + 1,
      connectionSucceeded: false // 重置连接状态
    });
    this.setData(newData);
    
    if (this.data.connectionAttempts > this.data.maxConnectionAttempts) {
      this.showStatus('连接尝试次数过多，请稍后再试', 'fail');
      // 提供手动重新连接的选项
      wx.showModal({
        title: '提示',
        content: '连接失败，是否重新尝试？',
        success: (res) => {
          if (res.confirm) {
            this.setData({ connectionAttempts: 0 });
            this.connectBLE(deviceId);
          } else {
            this.hideLoading();
          }
        }
      });
      return;
    }
    
    this.showLoading(`正在连接设备 (${this.data.connectionAttempts}/${this.data.maxConnectionAttempts})`);
    
    // 设置连接超时 - 使用统一定时器管理
    this.createTimer(`connect_${deviceId}`, this.data.connectTimeout, () => {
      if (this.data.connectionAttempts <= this.data.maxConnectionAttempts && !this.data.connectionSucceeded) {
        console.log('连接超时，尝试重新连接');
        wx.closeBLEConnection({
          deviceId: deviceId,
          success: () => console.log('已关闭超时连接')
        });
        this.connectBLE(deviceId);
      }
    });
    
    wx.createBLEConnection({
      deviceId: deviceId,
      timeout: this.data.connectTimeout,
      success: (res) => {
        this.setData({ connectionSucceeded: true }); // 更新连接状态
        this.clearTimer(`connect_${deviceId}`); // 清除超时定时器
        
        const currentDeviceName = this.getDeviceName(deviceId);
        const currentDevice = this.getDeviceInfo(deviceId);
        const connectSuccessData = Object.assign({}, this.data, {
          isConnected: true,
          deviceId: deviceId,
          currentDeviceName: currentDeviceName,
          deviceRSSI: currentDevice ? currentDevice.RSSI : '',
          connectionAttempts: 0
        });
        this.setData(connectSuccessData);
        
        this.clearReconnectTimer();
        this.showStatus('设备连接成功', 'success');
        
        // 保存设备ID
        if (deviceId !== this.data.savedDeviceId) {
          wx.setStorage({ key: 'savedDeviceId', data: deviceId });
        }
        
        // 获取服务和特征
        this.getDeviceServices(deviceId);
      },
      fail: (err) => {
        this.clearTimer(`connect_${deviceId}`); // 清除超时定时器
        console.error('连接失败:', err);
        let errorMsg = '连接失败';
        switch (err.errCode) {
          case 10003:
            errorMsg = '连接失败，请确保设备距离较近且未被其他设备连接';
            break;
          case 10012:
            errorMsg = '连接超时，请稍后重试';
            break;
          case 10013:
            errorMsg = '设备ID无效，请重新搜索设备';
            break;
          default:
            errorMsg = `连接失败: ${err.errMsg}`;
        }
        this.showStatus(errorMsg, 'fail');
        
        if (this.data.connectionAttempts < this.data.maxConnectionAttempts) {
          setTimeout(() => this.connectBLE(deviceId), 2000); // 2秒后重试
        } else {
          this.hideLoading();
        }
      }
    });
  },

  // 获取设备服务
  getDeviceServices(deviceId) {
    wx.getBLEDeviceServices({
      deviceId: deviceId,
      success: (res) => {
        console.log('获取服务成功:', res.services);
        
        // 查找目标服务（兼容短UUID）
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
        this.showStatus('获取服务失败: ' + err.errMsg, 'fail');
        console.error('获取服务失败详情:', err);
        this.hideLoading();
      }
    });
  },

  // 获取设备特征
  getDeviceCharacteristics(deviceId, serviceId) {
    wx.getBLEDeviceCharacteristics({
      deviceId: deviceId,
      serviceId: serviceId,
      success: (res) => {
        console.log('获取特征成功:', res.characteristics);
        
        let rxCharId = '';
        let txCharId = '';
        
        // 查找接收和发送特征（兼容短UUID）
        for (let i = 0; i < res.characteristics.length; i++) {
          const char = res.characteristics[i];
          if (this.normalizeUUID(char.uuid) === this.normalizeUUID(this.data.rxCharUUID)) {
            rxCharId = char.uuid;
            console.log('找到接收特征:', rxCharId);
          } else if (this.normalizeUUID(char.uuid) === this.normalizeUUID(this.data.txCharUUID)) {
            txCharId = char.uuid;
            console.log('找到发送特征:', txCharId);
            this.enableNotification(deviceId, serviceId, char.uuid);
          }
        }
        
        if (!rxCharId || !txCharId) {
          this.showStatus('未找到全部所需特征', 'fail');
          console.error('缺少特征:', { rxCharId: rxCharId, txCharId: txCharId });
          this.hideLoading();
        } else {
          this.setData({ rxCharId: rxCharId, txCharId: txCharId });
          this.showStatus('蓝牙初始化完成', 'success');
          
          // 连接成功后，分步获取状态
          setTimeout(() => {
            this.loadDeviceStatus();
          }, 800);
          this.hideLoading();
        }
      },
      fail: (err) => {
        this.showStatus('获取特征失败: ' + err.errMsg, 'fail');
        console.error('获取特征失败详情:', err);
        this.hideLoading();
      }
    });
  },

  // 启用特征通知
  enableNotification(deviceId, serviceId, characteristicId) {
    const enable = (retry = 0) => {
      wx.notifyBLECharacteristicValueChange({
        deviceId: deviceId,
        serviceId: serviceId,
        characteristicId: characteristicId,
        state: true,
        success: () => console.log('启用通知成功'),
        fail: (err) => {
          console.error('启用通知失败:', err);
          if (retry < 2) {
            setTimeout(() => enable(retry + 1), 1000);
          } else {
            this.showStatus('启用通知失败，请重新连接', 'fail');
          }
        }
      });
    };
    enable();
  },

  // 主动查询当前协议
  queryCurrentProtocol() {
    if (!this.data.isConnected) return;
    this.enqueueCommand('get_protocol', this.data.rxCharId);
    console.log('主动查询设备当前协议');
  },

  // 连接成功后，分步获取状态
  loadDeviceStatus() {
    // 1. 获取温湿度
    this.enqueueCommand('status', this.data.rxCharId);
    
    // 2. 获取电源状态
    setTimeout(() => {
      this.enqueueCommand('power', this.data.rxCharId);
      
      // 3. 获取协议
      setTimeout(() => {
        this.enqueueCommand('get_protocol', this.data.rxCharId);
      }, 300);
    }, 300);
  },

  // 处理接收到的数据
  processData(buffer) {
    try {
      const data = String.fromCharCode.apply(null, new Uint8Array(buffer));
      console.log('[BLE接收] 原始数据:', Array.from(new Uint8Array(buffer)));
      console.log('[BLE接收] 解析数据:', data);
      
      // 1. 解析新格式温湿度数据 (t25.5h44.6)
      if (data.startsWith('t')) {
        const tempMatch = data.match(/t([\d.]+)h([\d.]+)/);
        if (tempMatch) {
          const tempValue = tempMatch[1].toString();
          const humidityValue = tempMatch[2].toString();
          console.log('温湿度数据解析成功 (新格式):', { temp: tempValue, humidity: humidityValue });
          
          this.setData({
            temp: tempValue,
            humidity: humidityValue
          }, () => {
            console.log('温湿度数据更新成功');
          });
          return;
        }
      }
      
      // 2. 解析旧格式温湿度数据 (temp=25.5;humidity=44.6)
      const tempMatch = data.match(/temp=([\d.]+)/);
      const humidityMatch = data.match(/humidity=([\d.]+)/);
      if (tempMatch && humidityMatch) {
        let tempValue = tempMatch[1].toString();
        let humidityValue = humidityMatch[1].toString();
        console.log('温湿度数据解析成功 (旧格式):', { temp: tempValue, humidity: humidityValue });
        
        this.setData({
          temp: tempValue,
          humidity: humidityValue
        }, () => {
          console.log('温湿度数据更新成功');
        });
        
        // 解析状态中的协议
        const protocolMatchInStatus = data.match(/protocol=(\w+)/);
        if (protocolMatchInStatus && protocolMatchInStatus.length >= 2) {
          const protocol = protocolMatchInStatus[1];
          const protocolIndex = this.data.protocolList.indexOf(protocol);
          if (protocolIndex !== -1) {
            this.setData({ protocolIndex: protocolIndex });
            console.log('从状态中更新协议为:', protocol);
          }
        }
        // 解析状态中的电源
        const powerMatch = data.match(/power=(on|off)/);
        if (powerMatch && powerMatch.length >= 2) {
          this.setData({ isPowerOn: (powerMatch[1] === 'on') });
        }
        return;
      }
      
      // 3. 解析协议查询/设置返回（连接后自动发送的协议也走这里）
      const protocolMatch = data.match(/^protocol=(\w+)$/);
      if (protocolMatch && protocolMatch.length >= 2) {
        const protocol = protocolMatch[1];
        if (protocol === 'invalid') {
          this.showStatus('协议设置失败：无效的协议名称', 'fail');
        } else {
          const protocolIndex = this.data.protocolList.indexOf(protocol);
          if (protocolIndex !== -1) {
            this.setData({ protocolIndex: protocolIndex });
            // 连接时自动发送的协议不显示提示，避免干扰
            if (!this.data.isLoading) {
              this.showStatus(`当前协议已更新为: ${protocol}`, 'success');
            } else {
              console.log('协议已更新为:', protocol);
            }
          } else {
            this.showStatus(`未知协议: ${protocol}`, 'fail');
          }
        }
        return;
      }
      
      // 4. 解析协议学习结果
      const learnMatch = data.match(/learn=(\w+)(:(\w+))?/);
      if (learnMatch && learnMatch.length >= 2) {
        const status = learnMatch[1];
        if (status === 'success' && learnMatch.length >= 4) {
          const protocol = learnMatch[3];
          this.setData({ isLearning: false });
          const protocolIndex = this.data.protocolList.indexOf(protocol);
          if (protocolIndex !== -1) this.setData({ protocolIndex: protocolIndex });
          this.showStatus(`学习成功！已识别协议: ${protocol}`, 'success');
        } else if (status === 'timeout') {
          this.setData({ isLearning: false });
          this.showStatus('学习超时，请重试', 'fail');
        }
        return;
      }
      
      // 5. 解析电源状态返回
      const powerMatch = data.match(/power=(on|off)/);
      if (powerMatch && powerMatch.length >= 2) {
        this.setData({ isPowerOn: (powerMatch[1] === 'on') });
        this.showStatus(`空调${powerMatch[1] === 'on' ? '已开启' : '已关闭'}`, 'success');
        return;
      }
      
      // 6. 其他非温湿度数据只输出到控制台，不在界面显示
      if (data && !data.startsWith('temp=') && !data.startsWith('protocol=') && !data.startsWith('t')) {
        console.log('收到数据:', data);
      }
      
    } catch (error) {
      console.error('数据解析错误:', error);
      this.showStatus('数据解析失败', 'fail');
    }
  },

  // 断开蓝牙连接
  disconnectBLE() {
    this.clearReconnectTimer();
    
    if (this.data.isConnected && this.data.deviceId) {
      wx.closeBLEConnection({
        deviceId: this.data.deviceId,
        success: () => {
          this.setData({
            isConnected: false,
            deviceId: '',
            currentDeviceName: '',
            deviceRSSI: '',
            targetServiceId: '',
            rxCharId: '',
            txCharId: ''
          });
          this.showStatus('已断开连接', 'success');
        },
        fail: (err) => {
          this.showStatus('断开连接失败: ' + err.errMsg, 'fail');
        }
      });
    }
  },

  // 切换空调电源
  togglePower() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    
    const newPowerState = !this.data.isPowerOn;
    this.setData({ isPowerOn: newPowerState });
    
    if (newPowerState) {
      // 开空调时，默认设置为25度自动模式
      this.setData({
        targetTemp: 25,
        modeIndex: 0, // 自动模式
        speedIndex: 0  // 自动风速
      });
      // 发送完整设置
      this.sendSettings();
    } else {
      // 关空调时，只发送关闭命令
      const command = 'power=off';
      console.log('[发送命令] 电源切换:', command);
      this.enqueueCommand(command, this.data.rxCharId);
      this.showStatus('正在关闭空调...', '');
    }
  },

  // 查询设备状态
  queryDeviceStatus() {
    if (!this.data.isConnected) return;
    this.enqueueCommand('status', this.data.rxCharId);
  },

  // 开始学习模式
  startLearningMode() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    
    this.setData({ isLearning: true });
    this.showStatus('请按下空调遥控器任意按钮...', '');
    this.enqueueCommand('learn=start', this.data.rxCharId);
    
    // 设置学习超时 - 使用统一定时器管理
    this.createTimer('learn', 10000, () => {
      if (this.data.isLearning) {
        this.setData({ isLearning: false });
        this.showStatus('学习超时，请重试', 'fail');
      }
    });
  },

  // 获取设备名称
  getDeviceName(deviceId) {
    let deviceName = '未知设备';
    const deviceList = this.data.deviceList;
    for (let i = 0; i < deviceList.length; i++) {
      if (deviceList[i].deviceId === deviceId) {
        deviceName = deviceList[i].name || deviceList[i].deviceId;
        break;
      }
    }
    return deviceName;
  },

  getDeviceInfo(deviceId) {
    const deviceList = this.data.deviceList;
    for (let i = 0; i < deviceList.length; i++) {
      if (deviceList[i].deviceId === deviceId) {
        return deviceList[i];
      }
    }
    return null;
  },

  // 协议选择器控制
  showProtocolPicker() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    this.setData({ showProtocolPicker: true });
  },
  
  hideProtocolPicker() {
    this.setData({ showProtocolPicker: false });
  },
  
  // 模式选择器控制
  showModePicker() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    this.setData({ showModePicker: true });
  },
  
  hideModePicker() {
    this.setData({ showModePicker: false });
  },
  
  // 风速选择器控制
  showSpeedPicker() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    this.setData({ showSpeedPicker: true });
  },
  
  hideSpeedPicker() {
    this.setData({ showSpeedPicker: false });
  },
  
  // 设备选择器控制
  showDevicePicker() {
    if (this.data.deviceList.length <= 1) return;
    this.setData({ showDevicePicker: true });
  },
  
  hideDevicePicker() {
    this.setData({ showDevicePicker: false });
  },
  
  // 设备选择变更处理
  onDeviceChange(e) {
    const deviceId = e.currentTarget.dataset.deviceId;
    const currentDevice = this.getDeviceInfo(deviceId);
    this.setData({
      deviceId: deviceId,
      currentDeviceName: this.getDeviceName(deviceId),
      deviceRSSI: currentDevice ? currentDevice.RSSI : '',
      showDevicePicker: false
    });
    
    if (this.data.isConnected) {
      this.disconnectBLE();
    }
    
    setTimeout(() => this.connectBLE(deviceId), 500);
  },
  
  // 协议选择变更处理
  onProtocolChange(e) {
    const index = e.currentTarget.dataset.index;
    this.setData({ protocolIndex: index, showProtocolPicker: false });
    
    if (this.data.isConnected) {
      const protocol = this.data.protocolList[index];
      this.enqueueCommand(`protocol=${protocol}`, this.data.rxCharId);
      this.showStatus(`已设置协议: ${protocol}`, 'success');
    }
  },
  
  // 模式选择变更处理
  onModeChange(e) {
    const index = e.currentTarget.dataset.index;
    this.setData({ modeIndex: index, showModePicker: false });
    if (this.data.isConnected) this.sendSettings();
  },
  
  // 风速选择变更处理
  onSpeedChange(e) {
    const index = e.currentTarget.dataset.index;
    this.setData({ speedIndex: index, showSpeedPicker: false });
    if (this.data.isConnected) this.sendSettings();
  },

  quickSetTemp(e) {
    if (!this.data.isPowerOn) {
      this.showStatus('请先开启空调', 'fail');
      return;
    }
    const temp = Number(e.currentTarget.dataset.temp);
    if (temp >= 17 && temp <= 30) {
      this.setData({ targetTemp: temp });
      if (this.data.isConnected) this.sendSettings();
    }
  },

  quickSetMode(e) {
    if (!this.data.isPowerOn) {
      this.showStatus('请先开启空调', 'fail');
      return;
    }
    const index = Number(e.currentTarget.dataset.index);
    if (index >= 0 && index < this.data.modeList.length) {
      this.setData({ modeIndex: index });
      if (this.data.isConnected) this.sendSettings();
    }
  },

  quickSetSpeed(e) {
    if (!this.data.isPowerOn) {
      this.showStatus('请先开启空调', 'fail');
      return;
    }
    const index = Number(e.currentTarget.dataset.index);
    if (index >= 0 && index < this.data.speedList.length) {
      this.setData({ speedIndex: index });
      if (this.data.isConnected) this.sendSettings();
    }
  },

  // 增加温度
  increaseTemp() {
    if (!this.data.isPowerOn) {
      this.showStatus('请先开启空调', 'fail');
      return;
    }
    let temp = this.data.targetTemp;
    if (temp < 30) {
      this.setData({ targetTemp: temp + 1 });
      if (this.data.isConnected) this.sendSettings();
    } else {
      this.showStatus('温度已达最大值30℃', '');
    }
  },

  // 降低温度
  decreaseTemp() {
    if (!this.data.isPowerOn) {
      this.showStatus('请先开启空调', 'fail');
      return;
    }
    let temp = this.data.targetTemp;
    if (temp > 17) {
      this.setData({ targetTemp: temp - 1 });
      if (this.data.isConnected) this.sendSettings();
    } else {
      this.showStatus('温度已达最小值17℃', '');
    }
  },

  // 发送设置数据
  sendSettings() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    
    // 如果空调已关闭，不发送设置命令
    if (!this.data.isPowerOn) {
      return;
    }
    
    const temp = this.data.targetTemp;
    const mode = this.data.modeIndex;
    const speed = this.data.speedIndex;
    
    // 构建命令（仅发送参数，协议单独设置）
    const command = `temp=${temp};mode=${mode};speed=${speed};power=on`;
    this.enqueueCommand(command, this.data.rxCharId);
    const modeText = this.data.modeList[mode];
    const speedText = this.data.speedList[speed];
    this.showStatus(`已发送: ${temp}℃ ${modeText} ${speedText}`, 'success');
  },

  // 发送数据到蓝牙设备（带队列处理）
  enqueueCommand(command, characteristicId) {
    // 队列长度限制
    if (this.data.commandQueue.length >= this.data.maxQueueLength) {
      this.showStatus('命令队列已满，请稍后再试', 'fail');
      this.data.commandQueue.shift(); // 移除最旧命令
    }
    
    const commandItem = {
      command: command,
      characteristicId: characteristicId,
      timestamp: Date.now(),
      retries: 0
    };
    
    this.data.commandQueue.push(commandItem);
    if (!this.data.isSending) {
      this.processCommandQueue();
    }
  },

  // 处理命令队列
  processCommandQueue() {
    if (this.data.commandQueue.length === 0) {
      this.setData({ isSending: false });
      return;
    }
    
    // 检查命令间隔
    const now = Date.now();
    if (now - this.data.lastCommandTime < this.data.commandInterval) {
      setTimeout(() => this.processCommandQueue(), this.data.commandInterval - (now - this.data.lastCommandTime));
      return;
    }
    
    const commandItem = this.data.commandQueue[0];
    
    // 检查连接状态
    if (!this.data.isConnected || !this.data.targetServiceId || !commandItem.characteristicId) {
      this.showStatus('未连接设备或特征ID无效', 'fail');
      this.data.commandQueue.shift();
      this.processCommandQueue();
      return;
    }
    
    this.setData({ isSending: true, lastCommandTime: now });
    console.log('准备发送数据:', commandItem.command);
    
    wx.writeBLECharacteristicValue({
      deviceId: this.data.deviceId,
      serviceId: this.data.targetServiceId,
      characteristicId: commandItem.characteristicId,
      value: this.stringToArrayBuffer(commandItem.command),
      success: () => {
        console.log('数据发送成功:', commandItem.command);
        this.data.commandQueue.shift();
        this.processCommandQueue();
      },
      fail: (err) => {
        console.error('蓝牙写入失败:', err);
        commandItem.retries++;
        
        if (commandItem.retries >= this.data.maxCommandRetries) {
          this.showStatus(`命令发送失败: ${commandItem.command}`, 'fail');
          this.data.commandQueue.shift();
        }
        this.processCommandQueue();
      }
    });
  },

  // 字符串转ArrayBuffer
  stringToArrayBuffer(str) {
    const buffer = new ArrayBuffer(str.length);
    const dataView = new DataView(buffer);
    for (let i = 0; i < str.length; i++) {
      dataView.setUint8(i, str.charCodeAt(i));
    }
    return buffer;
  },

  // 显示状态信息
  showStatus(message, type) {
    this.setData({ status: message, statusType: type });
    const timeout = type === 'fail' ? 5000 : 3000;
    setTimeout(() => this.setData({ status: "" }), timeout);
  },

  // 显示加载提示
  showLoading(text) {
    this.setData({ isLoading: true, loadingText: text });
  },

  // 隐藏加载提示
  hideLoading() {
    this.setData({ isLoading: false });
  },

  // 跳转到定时任务页面
  goToTimer() {
    if (!this.data.isConnected) {
      this.showStatus('请先连接设备', 'fail');
      return;
    }
    wx.navigateTo({
      url: '/pages/timer/timer'
    });
  },

  // 安排自动重连
  scheduleReconnect() {
    this.clearReconnectTimer();
    this.reconnectTimer = setTimeout(() => {
      if (this.data.deviceId && !this.data.isConnected) {
        this.showStatus('尝试重新连接...', '');
        this.connectBLE(this.data.deviceId);
      }
    }, 5000);
  },

  // 清除重连定时器
  clearReconnectTimer() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  },

  // ========== 优化新增方法 ==========
  // UUID标准化（16位转128位BLE标准格式）
  normalizeUUID(uuid) {
    if (!uuid) return '';
    const clean = uuid.replace(/-/g, '').toUpperCase();
    // 16位UUID转128位标准格式
    if (clean.length === 4) {
      return `0000${clean}-0000-1000-8000-00805F9B34FB`.toUpperCase();
    }
    // 32位UUID补全分隔符
    if (clean.length === 32) {
      return `${clean.substring(0,8)}-${clean.substring(8,12)}-${clean.substring(12,16)}-${clean.substring(16,20)}-${clean.substring(20)}`.toUpperCase();
    }
    return clean;
  },

  // 创建定时器（统一管理）
  createTimer(type, delay, callback) {
    this.clearTimer(type); // 先清理旧定时器
    const timer = setTimeout(() => {
      callback();
      this.clearTimer(type); // 执行后清理
    }, delay);
    // 替换Object.assign，避免展开运算符
    const newTimers = Object.assign({}, this.data.timers);
    newTimers[type] = timer;
    this.setData({ timers: newTimers });
  },

  // 清除指定定时器
  clearTimer(type) {
    const timers = this.data.timers;
    if (timers[type]) {
      clearTimeout(timers[type]);
      const newTimers = Object.assign({}, timers);
      delete newTimers[type];
      this.setData({ timers: newTimers });
    }
  },

});
