# 一键发布并测试 ESP32 OTA
# 用法：
#   .\release_and_test_ota.ps1 -Environment esp32_wifi -Version 1.5.11 -DeviceIp 192.168.10.179
# 可选：
#   -SkipPush        只做本地发布，不推送 GitHub
#   -SkipPages       不同步 Pages 清单
#   -SkipDeviceTest  发布后不触发设备 OTA 测试
#   -Transport       默认 https

[CmdletBinding()]
param(
    [string]$Environment = 'esp32_wifi',
    [string]$Version = '',
    [string]$DeviceIp = '',
    [string]$Transport = 'https',
    [switch]$SkipRelease,
    [switch]$SkipPush,
    [switch]$SkipPages,
    [switch]$SkipDeviceTest
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

$configPath = Join-Path $PSScriptRoot 'src\DeviceConfig.h'
$configText = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8
$m = [regex]::Match($configText, '#define FW_VERSION "([^"]+)"')
if (-not $m.Success) { throw '无法读取当前 FW_VERSION' }
$oldVersion = $m.Groups[1].Value

if ([string]::IsNullOrWhiteSpace($Version)) {
    $parts = $oldVersion -split '\.'
    if ($parts.Count -lt 3) { throw "当前版本格式无法自动递增: $oldVersion" }
    $parts[2] = [string]([int]$parts[2] + 1)
    $Version = $parts -join '.'
}

if ($Version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') {
    throw "版本号 '$Version' 不是有效格式，例如 1.5.11"
}
if (-not $SkipRelease -and $Version -eq $oldVersion) {
    throw "新版本号和当前 v$oldVersion 相同，请指定更高版本号"
}

Write-Host "`n=== 一键发布并测试 OTA：v$Version / $Environment ===" -ForegroundColor Cyan

if (-not $SkipRelease) {
    Write-Host "[1/4] 编译、生成清单并提交..." -ForegroundColor Green
    $releaseCmd = "echo Y| release.bat -Environment $Environment -Version $Version -SkipDevice"
    if ($SkipPush) { $releaseCmd += " -SkipPush" }
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $releaseOutput = (& cmd /c $releaseCmd 2>&1 | Out-String)
    $ErrorActionPreference = $oldEap
    Write-Host $releaseOutput
    if ($releaseOutput -match 'Release failed') {
        throw "release.ps1 执行失败"
    }
}

if (-not $SkipPages) {
    Write-Host "[2/4] 同步 GitHub Pages 清单..." -ForegroundColor Green
    $manifestPath = Join-Path $PSScriptRoot "firmware\ota_$Environment.json"
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        throw "找不到清单文件：$manifestPath"
    }
    $b64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($manifestPath))
    $pagesPath = "firmware/ota_$Environment.json"
    $existingSha = gh api "repos/zhanghaompc/mqtt-control/contents/$pagesPath" --jq '.sha' 2>$null
    $putArgs = @(
        'api', '--method', 'PUT',
        "repos/zhanghaompc/mqtt-control/contents/$pagesPath",
        '-f', "message=sync OTA manifest v$Version",
        '-f', 'branch=main',
        '-f', "content=$b64"
    )
    if ($LASTEXITCODE -eq 0 -and $existingSha) {
        $putArgs += @('-f', "sha=$existingSha")
    }
    & gh @putArgs | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "GitHub Pages 清单上传失败"
    }
    Write-Host "Pages 清单已上传，等待 Pages 部署..." -ForegroundColor Green
    $pagesUrl = "https://zhanghaompc.github.io/mqtt-control/$pagesPath"
    $deadline = (Get-Date).AddSeconds(150)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 10
        try {
            $pagesBody = (Invoke-WebRequest -Uri $pagesUrl -UseBasicParsing -TimeoutSec 10).Content
            $pagesJson = $pagesBody | ConvertFrom-Json
            if ([string]$pagesJson.version -eq $Version) {
                Write-Host "Pages 清单已更新为 v$Version" -ForegroundColor Green
                break
            }
        } catch {
        }
    }
}

if ($SkipDeviceTest) {
    Write-Host "`n=== 发布完成，已跳过设备 OTA 测试 ===" -ForegroundColor Cyan
    return
}

if ([string]::IsNullOrWhiteSpace($DeviceIp)) {
    Write-Host "[3/4] 未提供 DeviceIp，跳过设备 OTA 测试" -ForegroundColor Yellow
    return
}

Write-Host "[3/4] 检查设备当前版本..." -ForegroundColor Green
$deviceUrl = "http://${DeviceIp}:8080"

function Get-JsonField([string]$url, [string]$field) {
    try {
        $r = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 10
        $j = $r.Content | ConvertFrom-Json
        return [string]$j.$field
    } catch {
        return ''
    }
}

$deviceFw = Get-JsonField "$deviceUrl/otaget" 'fw'
if (-not $deviceFw) {
    throw "无法访问设备 $DeviceIp，请确认设备和 WiFi 状态"
}
Write-Host "设备当前版本：$deviceFw" -ForegroundColor Yellow

if ($deviceFw -eq $Version) {
    Write-Host "设备已经是最新版本 v$Version，无需 OTA" -ForegroundColor Green
    return
}

$check = Invoke-WebRequest -Uri "$deviceUrl/otacheck" -UseBasicParsing -TimeoutSec 30
$checkJson = $check.Content | ConvertFrom-Json
Write-Host "检查结果：latest=$($checkJson.latest) update=$($checkJson.update)"

if ($checkJson.update -ne $true) {
    throw "设备没有发现新版本。请检查 GitHub 推送和 Pages 清单是否已生效。"
}

Write-Host "[4/4] 触发设备 OTA 下载并等待重启..." -ForegroundColor Green
try {
    $otaResp = Invoke-WebRequest -Uri "$deviceUrl/ota" -UseBasicParsing -TimeoutSec 240
    Write-Host $otaResp.Content
} catch {
    # 设备升级成功后可能直接重启，导致请求被截断；只要稍后版本号正确即可。
    Write-Host "OTA 请求结束（可能因设备重启断开）：$($_.Exception.Message)" -ForegroundColor Yellow
}

Start-Sleep -Seconds 12
$afterFw = Get-JsonField "$deviceUrl/otaget" 'fw'
Write-Host "OTA 后设备版本：$afterFw" -ForegroundColor Cyan
if ($afterFw -eq $Version) {
    Write-Host "`n=== OTA 测试通过：设备已从 $deviceFw 升级到 $afterFw ===" -ForegroundColor Green
} else {
    Write-Warning "OTA 后版本仍为 $afterFw，请检查设备串口日志。"
}
