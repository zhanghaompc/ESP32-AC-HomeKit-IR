# 一键发布 ESP32 WiFi 版固件到 GitHub master
# 用法：在项目根目录运行  .\publish.ps1   （或直接双击 publish.bat）
# 流程：改版本号 -> 编译 -> 复制固件 -> git 提交 -> 推送到 GitHub

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host " 固件发布工具 (ESP32 WiFi 版)" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

# ---------- 1. 版本号 ----------
$config = Join-Path $PSScriptRoot "src\DeviceConfig.h"
$configText = Get-Content $config -Raw -Encoding UTF8
$current = [regex]::Match($configText, '#define FW_VERSION "([^"]*)"').Groups[1].Value
$version = $args[0]
if (-not $version) {
    $input = Read-Host "当前版本 $current ，输入新版本号（直接回车保持 $current）"
    $version = if ($input) { $input } else { $current }
}
Write-Host "[1/5] 版本号: $current -> $version" -ForegroundColor Green
$newContent = $configText -replace '#define FW_VERSION "[^"]*"', ("#define FW_VERSION `"$version`"")
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($config, $newContent, $utf8NoBom)

# ---------- 2. 编译 ----------
$pio = (Get-Command pio -ErrorAction SilentlyContinue).Source
if (-not $pio) { $pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" }
Write-Host "[2/5] 编译 esp32_wifi ..." -ForegroundColor Green
& $pio run -e esp32_wifi
if ($LASTEXITCODE -ne 0) { throw "编译失败，请查看上面的错误信息" }

# ---------- 3. 复制固件 ----------
$bin = Join-Path $PSScriptRoot ".pio\build\esp32_wifi\firmware.bin"
$dest = Join-Path $PSScriptRoot "firmware\esp32_wifi.bin"
Copy-Item $bin $dest -Force
Write-Host "[3/5] 固件已更新: firmware\esp32_wifi.bin" -ForegroundColor Green

# ---------- 3.5 生成版本清单 ----------
$otaPath = Join-Path $PSScriptRoot "firmware\ota.json"
$otaJson = '{"version":"' + $version + '","url":"https://fastly.jsdelivr.net/gh/zhanghaompc/ESP32-AC-HomeKit-IR@master/firmware/esp32_wifi.bin"}'
[System.IO.File]::WriteAllText($otaPath, $otaJson, $utf8NoBom)
Write-Host "[3.5/5] 版本清单已生成: firmware\ota.json ($version)" -ForegroundColor Green

# ---------- 4. git 提交 ----------
git add src firmware publish.ps1 publish.bat
$commitMsg = git commit -m "bump firmware to v$version" 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "[4/5] 已提交" -ForegroundColor Green
}
elseif ($commitMsg -match 'nothing to commit') {
    Write-Host "[4/5] 没有新的改动，跳过提交" -ForegroundColor Yellow
}
else {
    Write-Host $commitMsg
    throw "git commit 失败，请检查上面的错误信息"
}

# ---------- 5. 推送 ----------
Write-Host "[5/5] 推送到 GitHub master ...（网络慢时可能需要一两分钟）" -ForegroundColor Green
$pushMsg = git push origin master 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host $pushMsg
    if ($pushMsg -match 'fetch first|non-fast-forward|rejected') {
        Write-Host "提示：远端和本地历史分叉了，可执行 git push --force origin master 对齐（个人固件仓库是安全的）" -ForegroundColor Yellow
    }
    throw "git push 失败，请检查网络后重试"
}

$sha = (git rev-parse HEAD).Trim()
Write-Host ""
Write-Host "=========== 发布完成 ===========" -ForegroundColor Cyan
Write-Host "固件版本: v$version"
Write-Host "提交:     $sha"
Write-Host "OTA 地址 (master):  https://fastly.jsdelivr.net/gh/zhanghaompc/ESP32-AC-HomeKit-IR@master/firmware/esp32_wifi.bin"
Write-Host "OTA 地址 (固定):    https://fastly.jsdelivr.net/gh/zhanghaompc/ESP32-AC-HomeKit-IR@$sha/firmware/esp32_wifi.bin"
Write-Host "=================================" -ForegroundColor Cyan
