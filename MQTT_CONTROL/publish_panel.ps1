# 一键把本地面板同步到 GitHub Pages（zhanghaompc/mqtt-control）
# 用法：在 MQTT_CONTROL 目录运行 .\publish_panel.ps1 （或双击 publish_panel.bat）

$ErrorActionPreference = 'Stop'
$repo = 'zhanghaompc/mqtt-control'
$branch = 'main'
$dir = $PSScriptRoot

function Invoke-GhPut($path, $b64, $message) {
    $tmp = Join-Path $env:TEMP ("ghpanel_" + [guid]::NewGuid().ToString('N') + '.json')
    # 更新已存在的文件必须带旧文件的 sha（新文件可以省略）
    $sha = $null
    $meta = gh api "repos/$repo/contents/$path" --jq '.sha' 2>$null
    if ($LASTEXITCODE -eq 0 -and $meta) { $sha = $meta.Trim() }
    $body = @{ message = $message; content = $b64; branch = $branch }
    if ($sha) { $body.sha = $sha }
    $body = $body | ConvertTo-Json -Compress
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($tmp, $body, $utf8NoBom)
    $out = gh api --method PUT "repos/$repo/contents/$path" --input $tmp 2>&1
    if ($LASTEXITCODE -ne 0) { Remove-Item $tmp -Force; throw "上传 $path 失败: $out" }
    Remove-Item $tmp -Force
    return ($out | ConvertFrom-Json)
}

Write-Host "=========== 面板发布 ===========" -ForegroundColor Cyan
Write-Host "目标: $repo@$branch"

$index = Join-Path $dir 'index.html'
$mqtt = Join-Path $dir 'mqtt.min.js'
if (!(Test-Path $index) -or !(Test-Path $mqtt)) { throw '缺少 index.html 或 mqtt.min.js' }

gh auth status 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'gh 未登录，请先运行 gh auth login' }

$msg = "sync panel from local (" + (Get-Date -Format 'yyyy-MM-dd HH:mm') + ")"

Write-Host "[1/2] 上传 index.html ..." -ForegroundColor Green
$b64i = [Convert]::ToBase64String([IO.File]::ReadAllBytes($index))
Invoke-GhPut 'index.html' $b64i $msg | Out-Null

Write-Host "[2/2] 上传 mqtt.min.js ..." -ForegroundColor Green
$b64m = [Convert]::ToBase64String([IO.File]::ReadAllBytes($mqtt))
Invoke-GhPut 'mqtt.min.js' $b64m $msg | Out-Null

Write-Host ""
Write-Host "=========== 发布完成 ===========" -ForegroundColor Cyan
Write-Host "在线面板: https://zhanghaompc.github.io/mqtt-control/"
Write-Host "CDN/Pages 缓存一般几分钟内自动刷新"
Write-Host "=================================" -ForegroundColor Cyan
