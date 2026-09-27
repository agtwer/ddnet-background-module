# ddnet-background-module —— 把模块打包成单文件 .dmod（就是个 zip 容器）
#
#   powershell -ExecutionPolicy Bypass -File pack-dmod.ps1
#
# module.json 是唯一事实来源：id / version 从它读；容器内的补丁路径与 module.json 的 patches 映射保持一致。
# 补丁本身不由本脚本生成（它来自游戏仓库：改好的树 git add -A + git diff --cached --binary）。
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Join-Path $root 'module.json'
if (-not (Test-Path $manifest)) { throw "缺少 module.json: $manifest" }

# 读清单（跳过 UTF-8 BOM，否则 ConvertFrom-Json 会报错）
$raw = [System.IO.File]::ReadAllText($manifest)
if ($raw.Length -gt 0 -and [int]$raw[0] -eq 0xFEFF) { $raw = $raw.Substring(1) }
$meta = $raw | ConvertFrom-Json
$id = $meta.id
$ver = $meta.version
if (-not $id -or -not $ver) { throw "module.json 里必须有 id 与 version" }

$OutDir = Join-Path $root 'dist'
$stage = Join-Path $env:TEMP "dmod-$id-$ver"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'patch') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'files') | Out-Null

$meta.patch = 'patch/ddnet-20.1.patch'
$meta.patches = [ordered]@{ 'ddnet@20.1' = 'patch/ddnet-20.1.patch' }
$json = $meta | ConvertTo-Json -Depth 6
[System.IO.File]::WriteAllText((Join-Path $stage 'module.json'), $json, (New-Object System.Text.UTF8Encoding($false)))

# Patch: DDNet 20.1 (patch\ddnet-20.1.patch -> patch/ddnet-20.1.patch).
$patchDdnet201 = Join-Path $root 'patch\ddnet-20.1.patch'
if (-not (Test-Path $patchDdnet201)) { throw "missing patch (DDNet 20.1 baseline): $patchDdnet201" }
Copy-Item $patchDdnet201 (Join-Path $stage 'patch\ddnet-20.1.patch') -Force

# 新增源文件（手工安装用）
Get-ChildItem (Join-Path $root 'files') -File | Copy-Item -Destination (Join-Path $stage 'files') -Force

# --- 打包（Compress-Archive 只认 .zip，所以压完再改名）-------------------------
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$out = Join-Path $OutDir "$id-$ver.dmod"
$tmp = Join-Path $env:TEMP "$id-$ver.zip"
if (Test-Path $tmp) { Remove-Item $tmp -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $tmp -CompressionLevel Optimal
Move-Item $tmp $out -Force
Remove-Item $stage -Recurse -Force

Write-Host "[ok] 已产出 $out" -ForegroundColor Green
Write-Host "     把它拖进 ddnet-module-installer（或复制到 <安装器>\mods\）即可安装本模块。"
Write-Host "     容器内容："
tar -tf $out | ForEach-Object { Write-Host "       $_" }
