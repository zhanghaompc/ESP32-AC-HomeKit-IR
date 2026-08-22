<script>
export default {
  globalData: {
    ble: {
      connected: false,
      deviceId: '',
      serviceId: '',
      rxCharId: '',
      txCharId: ''
    }
  },
  onLaunch() {
    console.log('App Launch')
  },
  onShow() {
    console.log('App Show')
  },
  onHide() {
    console.log('App Hide')
  }
}
</script>

<style>
page {
  min-height: 100%;
  /* ===== 设计令牌（Design Tokens）===== */
  --c-primary: #2563eb;
  --c-accent: #0ea5e9;
  --c-bg: #f4f7ff;
  --c-surface: #ffffff;
  --c-surface-soft: #f7faff;
  --c-border: #e6edf7;
  --c-text: #172033;
  --c-muted: #64748b;
  --c-success: #059669;
  --c-warning: #d97706;
  --c-error: #dc2626;
  --r-sm: 16rpx;
  --r-md: 24rpx;
  --r-lg: 32rpx;
  --shadow-card: 0 12rpx 32rpx rgba(37, 99, 235, 0.08);
  background:
    radial-gradient(1000rpx 720rpx at 88% -12%, rgba(37, 99, 235, 0.16) 0%, rgba(37, 99, 235, 0) 58%),
    radial-gradient(860rpx 640rpx at -12% 28%, rgba(14, 165, 233, 0.14) 0%, rgba(14, 165, 233, 0) 55%),
    linear-gradient(170deg, #f2f7ff 0%, #f8faff 42%, #f1fbfd 100%);
  color: var(--c-text);
  font-size: 28rpx;
  line-height: 1.5;
}

view,
text,
button,
scroll-view {
  box-sizing: border-box;
}

.container {
  padding: 32rpx 28rpx 60rpx;
}

.header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 28rpx;
}

.title {
  font-size: 42rpx;
  font-weight: 700;
  letter-spacing: 0.5rpx;
  color: #172033;
}

.ble-status {
  display: inline-flex;
  align-items: center;
  gap: 8rpx;
  padding: 10rpx 20rpx;
  border-radius: 999rpx;
  font-size: 24rpx;
  background: rgba(255, 255, 255, 0.82);
  border: 1rpx solid var(--c-border);
  box-shadow: 0 6rpx 18rpx rgba(37, 99, 235, 0.06);
}

.ble-status.connected {
  color: var(--c-success);
}

.ble-status.connecting {
  color: var(--c-warning);
  animation: status-breathe 1.4s ease-in-out infinite;
}

.ble-status.disconnected {
  color: var(--c-muted);
}

.modal-overlay,
.selector-overlay,
.loading-overlay {
  position: fixed;
  left: 0;
  top: 0;
  right: 0;
  bottom: 0;
  background: rgba(15, 23, 42, 0.45);
  z-index: 999;
  display: flex;
  align-items: center;
  justify-content: center;
}

.modal-overlay,
.selector-overlay {
  align-items: flex-end; /* 弹窗从底部滑出 */
}

.modal {
  width: 100%;
  max-height: 82vh;
  background: #f8faff;
  border-radius: 34rpx 34rpx 0 0;
  overflow: hidden;
  box-shadow: 0 -12rpx 48rpx rgba(15, 23, 42, 0.18);
  animation: sheet-up 0.24s ease-out;
}

.modal-header {
  padding: 32rpx 36rpx;
  font-size: 32rpx;
  font-weight: 700;
  color: #172033;
  background: #ffffff;
  border-bottom: 1rpx solid #eef2f7;
}

.modal-options {
  padding: 16rpx 0;
}

.modal-scroll {
  max-height: 66vh;
}

.option,
.selector-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 24rpx 34rpx;
  font-size: 28rpx;
  color: #374151;
}

.option.selected,
.selector-item.active {
  color: #2563eb;
  background: #eef4ff;
  font-weight: 600;
}

.loading-overlay {
  z-index: 1001;
}

.loading-spinner {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 18rpx;
  padding: 40rpx 48rpx;
  border-radius: 24rpx;
  background: #ffffff;
  box-shadow: 0 24rpx 80rpx rgba(15, 23, 42, 0.28);
}

.spinner {
  width: 56rpx;
  height: 56rpx;
  border: 6rpx solid #e5e7eb;
  border-top-color: #2563eb;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

.loading-text {
  font-size: 26rpx;
  color: #4b5563;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

@keyframes status-breathe {
  0%,
  100% {
    box-shadow: 0 6rpx 18rpx rgba(217, 119, 6, 0.08);
  }
  50% {
    box-shadow: 0 6rpx 26rpx rgba(217, 119, 6, 0.28);
  }
}

@keyframes sheet-up {
  from {
    transform: translateY(120rpx);
    opacity: 0.6;
  }
  to {
    transform: translateY(0);
    opacity: 1;
  }
}

@media (prefers-reduced-motion: reduce) {
  .ble-status.connecting,
  .icon-ring.scanning,
  .icon-ring.connecting,
  .icon-ring.reconnecting {
    animation: none !important;
  }
  .modal,
  .selector-container {
    animation: none !important;
  }
  .spinner {
    animation-duration: 1.2s;
  }
}
</style>
