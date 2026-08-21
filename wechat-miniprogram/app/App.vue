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
  background:
    radial-gradient(900rpx 700rpx at 85% -10%, rgba(96, 165, 250, 0.28) 0%, rgba(96, 165, 250, 0) 60%),
    radial-gradient(800rpx 600rpx at -15% 30%, rgba(45, 212, 191, 0.20) 0%, rgba(45, 212, 191, 0) 55%),
    linear-gradient(168deg, #dcecff 0%, #eef6ff 36%, #e9f8f2 68%, #f5f1ff 100%);
  color: #1f2937;
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
  font-size: 40rpx;
  font-weight: 700;
  color: #1f2937;
}

.ble-status {
  display: inline-flex;
  align-items: center;
  gap: 8rpx;
  padding: 10rpx 20rpx;
  border-radius: 999rpx;
  font-size: 24rpx;
  background: #ffffff;
  box-shadow: 0 6rpx 18rpx rgba(0, 0, 0, 0.06);
}

.ble-status.connected {
  color: #16a34a;
}

.ble-status.connecting {
  color: #d97706;
}

.ble-status.disconnected {
  color: #9ca3af;
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

.modal {
  width: 660rpx;
  max-height: 80vh;
  background: #ffffff;
  border-radius: 28rpx;
  overflow: hidden;
  box-shadow: 0 24rpx 80rpx rgba(15, 23, 42, 0.28);
}

.modal-header {
  padding: 30rpx 34rpx;
  font-size: 32rpx;
  font-weight: 700;
  border-bottom: 1rpx solid #eef2f7;
}

.modal-options {
  padding: 16rpx 0;
}

.modal-scroll {
  max-height: 60vh;
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
  color: #3a7bd5;
  background: #f0f6ff;
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
  border-top-color: #3a7bd5;
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
</style>
