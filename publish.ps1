# 一键发布 ESP32 固件到 GitHub master
# 用法：在项目根目录运行  .\publish.ps1 [env]
# env: esp32dev / esp32_wifi / esp32_ble，默认 esp32_wifi

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$envName = if ($args[0]) { $args[0] } else { "esp32_wifi" }
if ($envName -notin @("esp32dev", "esp32_wifi", "esp32_ble")) {
    throw "未知环境: $envName"
}

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host " 固件发布工具 ($envName)" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

function Invoke-Git {
    param([string[]]$ArgsList)
    $oldEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $outLines = & git @ArgsList 2>&1 | ForEach-Object { $_.ToString() }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $oldEAP
    return [pscustomobject]@{ Code = $code; Out = ($outLines -join "`n") }
}

$config = Join-Path $PSScriptRoot "src\DeviceConfig.h"
$configText = Get-Content $config -Raw -Encoding UTF8
$current = [regex]::Match($configText, '#define FW_VERSION "([^"]*)"').Groups[1].Value
$version = $args[1]
if (-not $version) {
    $input = Read-Host "当前版本 $current ，输入新版本号（直接回车保持 $current）"
    $version = if ($input) { $input } else { $current }
}
Write-Host "[1/5] 版本号: $current -> $version" -ForegroundColor Green
$newContent = $configText -replace '#define FW_VERSION "[^"]*"', ("#define FW_VERSION `"$version`"")
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($config, $newContent, $utf8NoBom)

$pio = (Get-Command pio -ErrorAction SilentlyContinue).Source
if (-not $pio) { $pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" }
Write-Host "[2/5] 编译 $envName ..." -ForegroundColor Green
& $pio run -e $envName
if ($LASTEXITCODE -ne 0) { throw "编译失败，请查看上面的错误信息" }

$bin = Join-Path $PSScriptRoot ".pio\build\$envName\firmware.bin"
$destDir = Join-Path $PSScriptRoot "firmware"
$dest = Join-Path $destDir "$envName.bin"
Copy-Item $bin $dest -Force
Write-Host "[3/5] 固件已更新: firmware\$envName.bin" -ForegroundColor Green

$manifestName = "ota_$envName.json"
$otaPath = Join-Path $destDir $manifestName
$repo = "zhanghaompc/ESP32-AC-HomeKit-IR"
$otaObj = [ordered]@{
    version = $version
    url = [ordered]@{
        $envName = "https://fastly.jsdelivr.net/gh/$repo@master/firmware/$envName.bin"
    }
}
[System.IO.File]::WriteAllText($otaPath, ($otaObj | ConvertTo-Json -Depth 4 -Compress), $utf8NoBom)
Write-Host "[3.5/5] 版本清单已生成: firmware\$manifestName" -ForegroundColor Green

Invoke-Git @('add', 'src', "firmware/$envName.bin", "firmware/$manifestName", 'publish.ps1', 'publish.bat') | Out-Null
$commitR = Invoke-Git @('commit', '-m', "bump $envName firmware to v$version")
if ($commitR.Code -eq 0) {
    Write-Host "[4/5] 已提交" -ForegroundColor Green
}
elseif ($commitR.Out -match "nothing to commit|no changes added to commit") {
    Write-Host "[4/5] 没有新的改动，跳过提交" -ForegroundColor Yellow
}
else {
    Write-Host $commitR.Out
    throw "git commit 失败，请检查上面的错误信息"
}

$sha = (git rev-parse HEAD).Trim()
$otaObj.url.$envName = "https://fastly.jsdelivr.net/gh/$repo@$sha/firmware/$envName.bin"
[System.IO.File]::WriteAllText($otaPath, ($otaObj | ConvertTo-Json -Depth 4 -Compress), $utf8NoBom)
Invoke-Git @('add', "firmware/$manifestName") | Out-Null
$manifestR = Invoke-Git @('commit', '-m', "update $envName ota manifest url @$sha")
if ($manifestR.Code -eq 0) {
    Write-Host "[4.5/5] 版本清单地址已固定到提交 $sha" -ForegroundColor Green
}
elseif ($manifestR.Out -match "nothing to commit|no changes added to commit") {
    Write-Host "[4.5/5] 版本清单无需修改" -ForegroundColor Yellow
}
else {
    Write-Host $manifestR.Out
    throw "git commit 清单失败，请检查上面的错误信息"
}

Write-Host "[5/5] 推送到 GitHub master ..." -ForegroundColor Green
$pushR = Invoke-Git @('push', 'origin', 'master')
Write-Host $pushR.Out
if ($pushR.Code -ne 0) {
    if ($pushR.Out -match "fetch first|non-fast-forward|rejected") {
        Write-Host "提示：远端和本地历史分叉了，可执行 git push --force origin master 对齐" -ForegroundColor Yellow
    }
    throw "git push 失败，请检查网络后重试"
}

$sha = (git rev-parse HEAD).Trim()
$devUrl = "https://fastly.jsdelivr.net/gh/$repo@$sha/firmware/$envName.bin"
$devIp = Read-Host "输入设备 IP 可自动设置 OTA 地址（回车跳过）"
if ($devIp) {
    $enc = [uri]::EscapeDataString($devUrl)
    try {
        $resp = Invoke-WebRequest -Uri "http://${devIp}:8080/otaset?url=$enc" -UseBasicParsing -TimeoutSec 8
        Write-Host "[5.5/5] 已自动设置设备 OTA 地址: $($resp.Content)" -ForegroundColor Green
    }
    catch {
        Write-Host "[5.5/5] 自动设置失败：$($_.Exception.Message)" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "=========== 发布完成 ===========" -ForegroundColor Cyan
Write-Host "固件版本: v$version"
Write-Host "环境:     $envName"
Write-Host "提交:     $sha"
Write-Host "设备 OTA 地址: $devUrl"
Write-Host "清单文件: firmware/$manifestName"
Write-Host "=================================" -ForegroundColor Cyan
