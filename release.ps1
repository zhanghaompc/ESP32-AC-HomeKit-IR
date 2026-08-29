# ESP32 一键发布脚本（Windows PowerShell 5.1 / PowerShell 7）
# 用法：双击 release.bat，或在项目根目录运行：
#   .\release.ps1
#   .\release.ps1 -Environment esp32_wifi -Version 1.5.2

[CmdletBinding()]
param(
    [string]$Environment,
    [string]$Version,
    [string]$Transport = 'ssh',
    [switch]$SkipPush,
    [switch]$SkipDevice
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Invoke-GitSafe {
    param([Parameter(Mandatory)][string[]]$Arguments)
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & git @Arguments 2>&1 | ForEach-Object { $_.ToString() }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $oldErrorActionPreference
    [pscustomobject]@{ Code = $code; Output = ($output -join "`n") }
}

function Read-Choice {
    param([string]$Prompt, [string[]]$Choices, [int]$Default = 1)
    while ($true) {
        Write-Host $Prompt
        for ($i = 0; $i -lt $Choices.Count; $i++) { Write-Host "  $($i + 1). $($Choices[$i])" }
        $raw = Read-Host "请选择 [$Default]"
        if ([string]::IsNullOrWhiteSpace($raw)) { return $Choices[$Default - 1] }
        $n = 0
        if ([int]::TryParse($raw, [ref]$n) -and $n -ge 1 -and $n -le $Choices.Count) { return $Choices[$n - 1] }
        Write-Host '输入无效，请重新选择。' -ForegroundColor Yellow
    }
}

function Ensure-GitHubHostKey {
    $sshDir = Join-Path $env:USERPROFILE '.ssh'
    $knownHosts = Join-Path $sshDir 'known_hosts'
    if (-not (Test-Path -LiteralPath $sshDir)) {
        New-Item -ItemType Directory -Path $sshDir -Force | Out-Null
    }

    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $existing = & ssh-keygen.exe -F github.com 2>$null
    $ErrorActionPreference = $oldEap
    if ($LASTEXITCODE -eq 0 -and $existing) { return }

    Write-Host '本机尚未记录 GitHub SSH 主机指纹，正在获取官方主机密钥：' -ForegroundColor Yellow
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $scan = @(& ssh-keyscan.exe -t ed25519,ecdsa,rsa github.com 2>$null)
    $scanCode = $LASTEXITCODE
    $ErrorActionPreference = $oldEap
    if ($scanCode -ne 0 -or $scan.Count -eq 0) {
        throw '无法获取 github.com SSH 主机密钥，请检查网络或手动执行 ssh-keyscan。'
    }

    Write-Host ($scan -join "`n") -ForegroundColor DarkGray
    $accept = Read-Host '请确认这是 GitHub 主机密钥，是否写入 known_hosts？(Y/N)'
    if ($accept -notmatch '^[Yy]$') { throw '已取消 SSH 主机验证。' }
    Add-Content -LiteralPath $knownHosts -Value ($scan -join "`n") -Encoding ASCII
    Write-Host "已写入 $knownHosts" -ForegroundColor Green
}

$configPath = Join-Path $PSScriptRoot 'src\DeviceConfig.h'
$configText = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8
$match = [regex]::Match($configText, '#define FW_VERSION "([^"]+)"')
if (-not $match.Success) { throw '无法从 src/DeviceConfig.h 读取 FW_VERSION' }
$oldVersion = $match.Groups[1].Value

if (-not $Environment) {
    $Environment = Read-Choice '选择要发布的固件环境：' @('esp32_wifi', 'esp32dev', 'esp32_ble') 1
}
if ($Environment -notin @('esp32_wifi', 'esp32dev', 'esp32_ble')) {
    throw "未知固件环境 '$Environment'。可选值：esp32_wifi、esp32dev、esp32_ble"
}
if ($Transport -notin @('ssh', 'https')) {
    throw "未知 Git 传输方式 '$Transport'。可选值：ssh、https"
}
if (-not $Version) { $Version = Read-Host "当前版本 v$oldVersion，输入新版本号（回车使用 $oldVersion）" }
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = $oldVersion }
if ($Version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') { throw "版本号 '$Version' 不是有效格式，例如 1.5.2" }

Write-Host "`n=== 发布 v$Version / $Environment ===" -ForegroundColor Cyan
$status = Invoke-GitSafe @('status', '--short')
if ($status.Code -ne 0) { throw "无法读取 Git 状态：$($status.Output)" }
if ($status.Output.Trim()) {
    Write-Host "检测到工作区已有改动：" -ForegroundColor Yellow
    Write-Host $status.Output
    $confirm = Read-Host '脚本只会提交本次发布相关文件，是否继续？(Y/N)'
    if ($confirm -notmatch '^[Yy]$') { throw '已取消发布' }
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$newConfigText = $configText -replace '#define FW_VERSION "[^"]+"', "#define FW_VERSION `"$Version`""
[IO.File]::WriteAllText($configPath, $newConfigText, $utf8NoBom)
$changedVersion = $true

try {
    $pio = (Get-Command pio -ErrorAction SilentlyContinue).Source
    if (-not $pio) { $pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe' }
    if (-not (Test-Path -LiteralPath $pio)) { throw '找不到 PlatformIO。请先安装 PlatformIO 或把 pio 加入 PATH。' }

    Write-Host "[1/6] 编译 $Environment ..." -ForegroundColor Green
    & $pio run -e $Environment
    if ($LASTEXITCODE -ne 0) { throw '编译失败，已停止发布。' }

    $buildBin = Join-Path $PSScriptRoot ".pio\build\$Environment\firmware.bin"
    if (-not (Test-Path -LiteralPath $buildBin)) { throw "找不到编译产物：$buildBin" }
    $firmwareDir = Join-Path $PSScriptRoot 'firmware'
    $destBin = Join-Path $firmwareDir "$Environment.bin"
    Copy-Item -LiteralPath $buildBin -Destination $destBin -Force

    $repo = 'zhanghaompc/ESP32-AC-HomeKit-IR'
    $manifestName = "ota_$Environment.json"
    $manifestPath = Join-Path $firmwareDir $manifestName
    $manifest = [ordered]@{ version = $Version; url = [ordered]@{ $Environment = "https://fastly.jsdelivr.net/gh/$repo@master/firmware/$Environment.bin" } }
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 4 -Compress), $utf8NoBom)

    # 保留旧版 ota.json 兼容入口，网页 OTA 使用 ota_esp32_wifi.json。
    $legacyPath = Join-Path $firmwareDir 'ota.json'
    if ($Environment -eq 'esp32_wifi') {
        $legacy = [ordered]@{ version = $Version; url = "https://fastly.jsdelivr.net/gh/$repo@master/firmware/$Environment.bin" }
        [IO.File]::WriteAllText($legacyPath, ($legacy | ConvertTo-Json -Compress), $utf8NoBom)
    }

    Write-Host "[2/6] 固件和 OTA 清单已生成" -ForegroundColor Green
    $add = @('add', 'src/DeviceConfig.h', "firmware/$Environment.bin", "firmware/$manifestName", 'release.ps1', 'release.bat')
    if ($Environment -eq 'esp32_wifi') { $add += 'firmware/ota.json' }
    $r = Invoke-GitSafe $add
    if ($r.Code -ne 0) { throw "git add 失败：$($r.Output)" }
    $r = Invoke-GitSafe @('commit', '-m', "release $Environment firmware v$Version")
    if ($r.Code -ne 0) { throw "git commit 失败：$($r.Output)" }
    $firstSha = (git rev-parse HEAD).Trim()

    # 清单固定到本次固件提交，避免 CDN 的 master 地址短时间取到旧文件。
    $manifest.url.$Environment = "https://fastly.jsdelivr.net/gh/$repo@$firstSha/firmware/$Environment.bin"
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 4 -Compress), $utf8NoBom)
    if ($Environment -eq 'esp32_wifi') {
        $legacy.url = "https://fastly.jsdelivr.net/gh/$repo@$firstSha/firmware/$Environment.bin"
        [IO.File]::WriteAllText($legacyPath, ($legacy | ConvertTo-Json -Compress), $utf8NoBom)
    }
    $r = Invoke-GitSafe @('add', "firmware/$manifestName")
    if ($r.Code -ne 0) { throw "git add 清单失败：$($r.Output)" }
    if ($Environment -eq 'esp32_wifi') {
        $r = Invoke-GitSafe @('add', 'firmware/ota.json')
        if ($r.Code -ne 0) { throw "git add 兼容清单失败：$($r.Output)" }
    }
    $r = Invoke-GitSafe @('commit', '-m', "pin $Environment OTA binary @$firstSha")
    if ($r.Code -ne 0) { throw "git commit 清单失败：$($r.Output)" }
    $sha = (git rev-parse HEAD).Trim()

    if (-not $SkipPush) {
        if ($Transport -eq 'ssh') { Ensure-GitHubHostKey }
        $remoteUrl = if ($Transport -eq 'ssh') { 'git@github.com:zhanghaompc/ESP32-AC-HomeKit-IR.git' } else { 'https://github.com/zhanghaompc/ESP32-AC-HomeKit-IR.git' }
        $remoteSet = Invoke-GitSafe @('remote', 'set-url', 'origin', $remoteUrl)
        if ($remoteSet.Code -ne 0) { throw "设置 Git 远程地址失败：$($remoteSet.Output)" }
        Write-Host "[3/6] 使用 $Transport 推送到 GitHub origin/master ..." -ForegroundColor Green
        $r = Invoke-GitSafe @('push', 'origin', 'master')
        if ($r.Code -ne 0) {
            if ($Transport -eq 'ssh') {
                throw "SSH 推送失败：$($r.Output)`n请先配置 GitHub SSH Key，或临时使用 -Transport https。"
            }
            throw "git push 失败：$($r.Output)"
        }
    } else { Write-Host '[3/6] 已跳过 GitHub 推送' -ForegroundColor Yellow }

    $otaUrl = "https://fastly.jsdelivr.net/gh/$repo@$firstSha/firmware/$Environment.bin"
    if (-not $SkipDevice) {
        $deviceIp = Read-Host '输入设备 IP 可自动设置 OTA 地址（回车跳过）'
        if ($deviceIp) {
            $encoded = [uri]::EscapeDataString($otaUrl)
            try {
                $resp = Invoke-WebRequest -Uri "http://${deviceIp}:8080/otaset?url=$encoded" -UseBasicParsing -TimeoutSec 8
                Write-Host "[4/6] 设备 OTA 地址已设置：$($resp.Content)" -ForegroundColor Green
            } catch { Write-Host "[4/6] 自动设置失败：$($_.Exception.Message)" -ForegroundColor Yellow }
        }
    } else { Write-Host '[4/6] 已跳过设备地址设置' -ForegroundColor Yellow }

    Write-Host "`n=== 发布完成 ===" -ForegroundColor Cyan
    Write-Host "版本：v$Version"
    Write-Host "环境：$Environment"
    Write-Host "提交：$sha"
    Write-Host "OTA：$otaUrl"
    Write-Host "清单：firmware/$manifestName"
} catch {
    if ($changedVersion) { [IO.File]::WriteAllText($configPath, $configText, $utf8NoBom) }
    throw
}
