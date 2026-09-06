// 微信小程序 / uni-app App 缺少这些浏览器/Node 全局对象，mqtt.js 启动时会用到
// 在引入 mqtt 之前加载，补齐最小实现避免白屏与连接失败
if (typeof globalThis !== 'undefined') {
  if (!globalThis.self) globalThis.self = globalThis
  if (!globalThis.window) globalThis.window = globalThis
  if (!globalThis.navigator) {
    globalThis.navigator = { language: 'zh-CN', userAgent: 'uni-app', platform: 'devtools' }
  }
  if (!globalThis.AbortController) {
    function AbortSignalPoly() {
      this.aborted = false
      this.reason = null
      this._listeners = []
    }
    AbortSignalPoly.prototype.addEventListener = function (type, cb) {
      if (type === 'abort' && typeof cb === 'function') this._listeners.push(cb)
    }
    AbortSignalPoly.prototype.removeEventListener = function () {}
    AbortSignalPoly.prototype.dispatchEvent = function () { return true }
    AbortSignalPoly.prototype.throwIfAborted = function () {}
    function AbortControllerPoly() {
      this.signal = new AbortSignalPoly()
    }
    AbortControllerPoly.prototype.abort = function (reason) {
      if (this.signal.aborted) return
      this.signal.aborted = true
      this.signal.reason = reason
      var ls = this.signal._listeners
      for (var i = 0; i < ls.length; i++) { try { ls[i]() } catch (e) {} }
    }
    globalThis.AbortController = AbortControllerPoly
    globalThis.AbortSignal = AbortSignalPoly
  }
}

// uni-app App 运行环境没有全局 WebSocket，用 uni.connectSocket 实现标准 WebSocket 接口供 mqtt.js 使用
if (typeof globalThis !== 'undefined' && !globalThis.WebSocket && typeof uni !== 'undefined') {
  function UniWebSocket(url, protocols) {
    this.readyState = 0
    this.onopen = null
    this.onmessage = null
    this.onclose = null
    this.onerror = null
    this.binaryType = 'arraybuffer'
    var self = this
    this._task = uni.connectSocket({ url: url, complete: function () {} })
    this._task.onOpen(function () {
      self.readyState = 1
      if (self.onopen) self.onopen({ type: 'open', target: self })
    })
    this._task.onMessage(function (res) {
      if (self.onmessage) self.onmessage({ type: 'message', data: res.data, target: self })
    })
    this._task.onClose(function (res) { console.log('[WS] close', res)
      self.readyState = 3
      if (self.onclose) self.onclose({ type: 'close', code: res && res.code, reason: res && res.reason, target: self })
    })
    this._task.onError(function (res) { console.log('[WS] error', res)
      if (self.onerror) self.onerror({ type: 'error', message: (res && res.errMsg) || 'websocket error', target: self })
    })
  }
  UniWebSocket.prototype.send = function (data) {
    this._task.send({ data: data })
  }
  UniWebSocket.prototype.close = function (code, reason) {
    try {
      if (typeof code === 'number' && (code === 1000 || (code >= 3000 && code <= 4999))) {
        this._task.close({ code: code, reason: reason })
      } else {
        this._task.close({})
      }
    } catch (e) {}
  }
  UniWebSocket.CONNECTING = 0
  UniWebSocket.OPEN = 1
  UniWebSocket.CLOSING = 2
  UniWebSocket.CLOSED = 3
  globalThis.WebSocket = UniWebSocket
}