# ddnet-background-module - one-click installer (Windows / PowerShell)
#
#   powershell -ExecutionPolicy Bypass -File install.ps1                       # everything, into .\myclient
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Mirror hk.gh-proxy.org
#   powershell -ExecutionPolicy Bypass -File install.ps1 -MirrorOff             # direct GitHub (no mirror)
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Proxy 127.0.0.1:7890  # local HTTP proxy
#   powershell -ExecutionPolicy Bypass -File install.ps1 -WorkDir D:\games\myclient
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Source C:\src\ddnet-20.1   # patch an existing tree
#   powershell -ExecutionPolicy Bypass -File install.ps1 -SkipBuild                  # stop after patching
#
# Steps: clone at the pinned base commit -> submodules -> apply patch -> FFmpeg 8.1
#        -> build -> assemble a double-clickable folder under <WorkDir>\dist
#
# ASCII-only on purpose: Windows PowerShell 5.1 reads UTF-8-without-BOM .ps1 as ANSI
# and non-ASCII comments break parsing.
param(
    [string]$WorkDir = (Join-Path (Get-Location) 'myclient'),
    [string]$Source = '',
    [string]$Repo = 'https://github.com/ddnet/ddnet.git',
    [string]$BaseCommit = '20.1',
    [string]$Mirror = 'ghproxy.net',
    [switch]$MirrorOff,
    [string]$Proxy = '',
    [switch]$SkipSubmodules,
    [switch]$SkipFfmpeg,
    [string]$FfmpegZip = '',
    [switch]$SkipBuild,
    [string]$BuildDir = 'build',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$moduleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$applyScript = Join-Path $moduleDir 'apply.ps1'

# --- network options (mirror is optional; proxy is optional) -----------------
# Mirror: host name, e.g. ghproxy.net or hk.gh-proxy.org ("https://" and trailing "/" are tolerated).
$MirrorHost = $Mirror.Trim()
$MirrorHost = $MirrorHost -replace '^https?://', ''
$MirrorHost = $MirrorHost.TrimEnd('/')
$UseMirror = (-not $MirrorOff) -and ($MirrorHost -ne '')
# Proxy: e.g. 127.0.0.1:7890 (scheme optional); empty = no proxy.
$ProxyUrl = $Proxy.Trim().TrimEnd('/')
if ($ProxyUrl -ne '' -and $ProxyUrl -notmatch '^https?://') { $ProxyUrl = "http://$ProxyUrl" }
# One git config list used by every git command (clone / fetch / submodule all inherit it,
# so submodule URLs and the 580MB ddnet-libs download go through the mirror and proxy too).
$GitNet = @()
if ($UseMirror) { $GitNet += @('-c', "url.https://$MirrorHost/https://github.com/.insteadOf=https://github.com/") }
if ($ProxyUrl -ne '') { $GitNet += @('-c', "http.proxy=$ProxyUrl", '-c', "https.proxy=$ProxyUrl") }

function Say($msg, $color = 'Gray') { Write-Host $msg -ForegroundColor $color }
function Step($n, $msg) { Write-Host ""; Write-Host "[$n] $msg" -ForegroundColor Cyan }

if ($UseMirror) { Say "[i] network: mirror $MirrorHost" 'Cyan' } else { Say '[i] network: direct GitHub (no mirror)' 'Cyan' }
if ($ProxyUrl -ne '') { Say "[i] network: proxy $ProxyUrl" 'Cyan' } else { Say '[i] network: no proxy' 'Cyan' }

# --- 0. sanity ---------------------------------------------------------------
Step 0 'Checking tools'
foreach ($t in @('git', 'cmake')) {
    if (-not (Get-Command $t -ErrorAction SilentlyContinue)) {
        Say "  '$t' not found on PATH." 'Red'
        if ($t -eq 'cmake') { Say '  Use the Developer PowerShell of Visual Studio, or add CMake to PATH.' 'Yellow' }
        exit 1
    }
    Say "  ok: $((Get-Command $t).Source)"
}
if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) { Say '  warning: cargo (Rust) not found; the build will likely fail - install Rust first.' 'Yellow' }

# --- 1. source tree ---------------------------------------------------------
if ($Source -ne '') {
    $tree = (Resolve-Path $Source).Path
    Step 1 "Using existing source tree: $tree"
} else {
    Step 1 "Cloning $Repo at base commit $BaseCommit"
    $url = $Repo   # mirror + proxy come from the -c git config in $GitNet
    if (Test-Path $WorkDir) {
        if (Test-Path (Join-Path $WorkDir '.git')) { Say "  reusing existing checkout: $WorkDir" }
        else { Say "  $WorkDir exists and is not a git repo - remove it or pick another -WorkDir" 'Red'; exit 1 }
    } else {
        New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
        Push-Location $WorkDir
        & git @GitNet init -q .
        & git @GitNet remote add origin $url
        Say "  fetching $BaseCommit from $url (shallow; $(if ($UseMirror) { "via $MirrorHost" } else { 'direct' }))..."
        & git @GitNet fetch --depth 1 origin $BaseCommit
        if ($LASTEXITCODE -ne 0) {
            Say '  shallow fetch of the exact commit failed; falling back to a full clone...' 'Yellow'
            Pop-Location
            Remove-Item $WorkDir -Recurse -Force
            & git @GitNet clone $url $WorkDir
            if ($LASTEXITCODE -ne 0) { Say '  clone failed. Check the network/mirror, or pass -Proxy 127.0.0.1:7890' 'Red'; exit 1 }
            Push-Location $WorkDir
        }
        & git @GitNet checkout -q -B background-module FETCH_HEAD
        Pop-Location
        Say "  ok: $(git -C $WorkDir log --oneline -1)"
    }
    $tree = (Resolve-Path $WorkDir).Path
}

# --- 2. submodules ----------------------------------------------------------
if (-not $SkipSubmodules) {
    Step 2 'Initialising submodules (ddnet-libs ~580MB; mirror helps here)'
    Push-Location $tree
    & git @GitNet submodule update --init --recursive
    $rc = $LASTEXITCODE
    Pop-Location
    if ($rc -ne 0) { Say '  submodule update failed (network). Re-run, try another mirror, or pass -Proxy 127.0.0.1:7890' 'Red'; exit 1 }
    Say '  ok'
} else { Step 2 'Submodules skipped' }

# --- 3. patch ---------------------------------------------------------------
Step 3 'Applying the background module patch'
& powershell -NoProfile -ExecutionPolicy Bypass -File $applyScript -Target $tree
if ($LASTEXITCODE -ne 0) { Say '  apply failed (see message above).' 'Red'; exit 1 }

# --- 4. FFmpeg 8.1 (video support) -----------------------------------------
if (-not $SkipFfmpeg) {
    Step 4 'Installing FFmpeg 8.1 (needed for video backgrounds)'
    $zipUrl = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.1-latest-win64-gpl-shared-8.1.zip'
    if ($FfmpegZip -eq '') {
        $FfmpegZip = Join-Path $env:TEMP 'ffmpeg-n8.1-win64-gpl-shared.zip'
        if (-not (Test-Path $FfmpegZip)) {
            $dl = if ($UseMirror) { "https://$MirrorHost/$zipUrl" } else { $zipUrl }
            Say "  downloading $dl"
            $iwr = @{ Uri = $dl; OutFile = $FfmpegZip; UseBasicParsing = $true; TimeoutSec = 900 }
            if ($ProxyUrl -ne '') { $iwr['Proxy'] = $ProxyUrl }
            try { Invoke-WebRequest @iwr }
            catch {
                Say '  download failed. Grab it by hand from BtbN/FFmpeg-Builds (asset ffmpeg-n8.1-latest-win64-gpl-shared-8.1.zip)' 'Yellow'
                Say '  then re-run with:  -FfmpegZip <path-to-zip>' 'Yellow'
                Say '  (without FFmpeg images still work, video does not)' 'Yellow'
                $FfmpegZip = ''
            }
        }
    }
    if ($FfmpegZip -ne '' -and (Test-Path $FfmpegZip)) {
        $ex = Join-Path $env:TEMP 'ffmpeg-8.1-extract'
        if (Test-Path $ex) { Remove-Item $ex -Recurse -Force }
        Expand-Archive -Path $FfmpegZip -DestinationPath $ex -Force
        $root = (Get-ChildItem $ex -Directory | Select-Object -First 1).FullName
        $libs = Join-Path $tree 'ddnet-libs\ffmpeg'
        Copy-Item (Join-Path $root 'include\*') (Join-Path $libs 'include') -Recurse -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $libs 'windows\lib') | Out-Null
        Copy-Item (Join-Path $root 'lib\*.lib') (Join-Path $libs 'windows\lib') -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $tree 'thirdparty-dll') | Out-Null
        foreach ($d in @('avcodec-62.dll','avformat-62.dll','avutil-60.dll','swresample-6.dll','swscale-9.dll')) {
            $src = Join-Path $root "bin\$d"
            if (Test-Path $src) { Copy-Item $src (Join-Path $tree 'thirdparty-dll') -Force } else { Say "  missing in archive: $d" 'Yellow' }
        }
        # stamp headers so CMake/compiler cannot keep stale objects (ABI mismatch => 0xc0000409 crash)
        Get-ChildItem (Join-Path $libs 'include') -Recurse -File | ForEach-Object { $_.LastWriteTime = Get-Date }
        Say '  ok: headers, import libs and runtime DLLs staged (headers touched to force a rebuild)'
    }
} else { Step 4 'FFmpeg step skipped (images only)' }

if ($SkipBuild) { Step 9 "Done (patch only). Build later with cmake in $tree"; exit 0 }

# --- 5. build ---------------------------------------------------------------
Step 5 "Configuring CMake ($BuildDir)"
Push-Location $tree
& cmake -S . -B $BuildDir -A x64 -DVULKAN=OFF -DDOWNLOAD_GTEST=OFF
if ($LASTEXITCODE -ne 0) { Pop-Location; Say '  cmake configure failed.' 'Red'; exit 1 }
Step 6 "Building game-client ($Config)"
& cmake --build $BuildDir --config $Config --target game-client --parallel 4
$rc = $LASTEXITCODE
Pop-Location
if ($rc -ne 0) { Say '  build failed.' 'Red'; exit 1 }

# --- 7. assemble ------------------------------------------------------------
Step 7 'Assembling a double-clickable folder'
$dist = Join-Path $tree 'dist'
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item (Join-Path $tree "$BuildDir\$Config\DDNet.exe") $dist -Force
Get-ChildItem (Join-Path $tree $BuildDir) -Filter '*.dll' -File | Copy-Item -Destination $dist -Force
if (Test-Path (Join-Path $tree 'thirdparty-dll')) { Copy-Item (Join-Path $tree 'thirdparty-dll\*.dll') $dist -Force }
Copy-Item (Join-Path $tree 'data') $dist -Recurse -Force
Copy-Item (Join-Path $tree 'storage.cfg') $dist -Force
@"
DDNet 20.1 + custom background module

Run:  double-click DDNet.exe
Use:  put images/videos into  %APPDATA%\DDNet\Background
      then  Settings -> Background  -> pick the file, enable the menu/entity toggles
Note: config and data live in %APPDATA%\DDNet (standard save location)
"@ | Set-Content (Join-Path $dist 'README.txt') -Encoding ASCII

Step 8 'Verifying the result'
$exe = Join-Path $dist 'DDNet.exe'
$dlls = (Get-ChildItem $dist -Filter '*.dll' -File | Measure-Object).Count
$hasVideo = (Test-Path (Join-Path $dist 'avcodec-62.dll'))
Say "  $exe  ($([int]((Get-Item $exe).Length/1KB)) KB)"
Say "  $dlls DLL(s) next to it; FFmpeg 8.1 present: $hasVideo"
Say "  $dist is ready - double-click DDNet.exe" 'Green'
