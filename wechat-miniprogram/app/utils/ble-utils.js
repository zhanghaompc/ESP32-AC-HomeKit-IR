// 16 位 UUID 转 128 位标准 BLE UUID，兼容短 UUID 比较
export function normalizeUUID(uuid) {
  if (!uuid) return ''
  const clean = String(uuid).replace(/-/g, '').toUpperCase()
  if (clean.length === 4) {
    return `0000${clean}-0000-1000-8000-00805F9B34FB`.toUpperCase()
  }
  if (clean.length === 32) {
    return `${clean.substring(0, 8)}-${clean.substring(8, 12)}-${clean.substring(12, 16)}-${clean.substring(16, 20)}-${clean.substring(20)}`.toUpperCase()
  }
  return clean
}

export function stringToArrayBuffer(str) {
  const buffer = new ArrayBuffer(str.length)
  const dataView = new DataView(buffer)
  for (let i = 0; i < str.length; i++) {
    dataView.setUint8(i, str.charCodeAt(i))
  }
  return buffer
}

export function arrayBufferToString(buffer) {
  const bytes = buffer instanceof ArrayBuffer ? new Uint8Array(buffer) : new Uint8Array(buffer)
  // 按 UTF-8 解码，支持设备返回的中文（如 time=未同步）
  let out = ''
  let i = 0
  while (i < bytes.length) {
    const b = bytes[i]
    if (b < 0x80) {
      out += String.fromCharCode(b)
      i += 1
    } else if (b < 0xe0) {
      out += String.fromCharCode(((b & 0x1f) << 6) | (bytes[i + 1] & 0x3f))
      i += 2
    } else if (b < 0xf0) {
      out += String.fromCharCode(((b & 0x0f) << 12) | ((bytes[i + 1] & 0x3f) << 6) | (bytes[i + 2] & 0x3f))
      i += 3
    } else {
      out += String.fromCharCode(
        ((b & 0x07) << 18) | ((bytes[i + 1] & 0x3f) << 12) | ((bytes[i + 2] & 0x3f) << 6) | (bytes[i + 3] & 0x3f)
      )
      i += 4
    }
  }
  return out
}

// 全局蓝牙写入串行队列：避免首页与定时页同时写特征值导致 Android 报 10007
let writeChain = Promise.resolve()

export function enqueueWrite(task) {
  const run = writeChain.then(task, task)
  writeChain = run.catch(() => {})
  return run
}

export function getPlatform() {
  try {
    return uni.getSystemInfoSync().platform || 'unknown'
  } catch (e) {
    return 'unknown'
  }
}

// 原生安卓在扫描 BLE 前需要运行时权限（Android 11 及以下依赖定位，12+ 依赖蓝牙权限）
export function ensureNativePermissions() {
  return new Promise((resolve) => {
    // plus 仅在原生 App 环境存在
    if (typeof plus === 'undefined' || getPlatform() !== 'android') {
      resolve()
      return
    }

    const permissions = [
      'android.permission.ACCESS_FINE_LOCATION',
      'android.permission.ACCESS_COARSE_LOCATION',
      'android.permission.BLUETOOTH_SCAN',
      'android.permission.BLUETOOTH_CONNECT'
    ]

    try {
      plus.android.requestPermissions(
        permissions,
        () => resolve(),
        () => resolve()
      )
    } catch (e) {
      // 老版本系统可能没有 BLUETOOTH_SCAN/CONNECT，直接继续，由 BLE API 兜底报错
      resolve()
    }
  })
}
