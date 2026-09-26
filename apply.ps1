# ddnet-background-module - installer (Windows / PowerShell)
#
# Applies the custom-background feature to a DDNet-family client source tree.
#
#   powershell -ExecutionPolicy Bypass -File apply.ps1 -Target C:\path\to\client
#
# Options:
#   -Target <dir>   source tree to patch (default: current directory)
#   -CheckOnly      only test whether the patch applies cleanly
#   -Revert         remove the feature again (reverse-apply)
#   -Reject         apply what fits and leave .rej files for the rest
#                   (use this on a base other than TClient 10.9.0)
param(
    [string]$Target = ".",
    [switch]$CheckOnly,
    [switch]$Revert,
    [switch]$Reject
)

$ErrorActionPreference = 'Stop'
$moduleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$patch = Join-Path $moduleDir 'patch\ddnet-background.patch'

if (-not (Test-Path $patch)) { throw "patch not found: $patch" }
if (-not (Test-Path $Target)) { throw "target not found: $Target" }
$Target = (Resolve-Path $Target).Path

# --- sanity + base detection -------------------------------------------------
$marker = Join-Path $Target 'src\game\client\components\menus.cpp'
if (-not (Test-Path $marker)) {
    throw "'$Target' does not look like a DDNet-family client source tree (src\game\client\components\menus.cpp missing)"
}
$isTClient = Test-Path (Join-Path $Target 'src\engine\shared\config_variables_tclient.h')
$base = if ($isTClient) { 'TClient 10.9.0 (verified)' } else { 'DDNet official / other fork (unverified)' }
Write-Host "Detected base: $base" -ForegroundColor Cyan
if (-not $isTClient -and -not $Reject -and -not $CheckOnly) {
    Write-Host "[i] This base is not the verified one. Re-run with -Reject" -ForegroundColor Yellow
    Write-Host "    to apply the parts that fit and leave .rej files for the rest (each hunk carries its own anchor)." -ForegroundColor Yellow
}
$already = Test-Path (Join-Path $Target 'src\game\client\components\custom_background.cpp')
if ($already -and -not $Revert) {
    Write-Host "[!] this tree already contains the background feature (custom_background.cpp present)." -ForegroundColor Yellow
    Write-Host "    Nothing to do. Use -Revert to remove it." -ForegroundColor Yellow
    exit 0
}

# --- apply / check / revert --------------------------------------------------
Push-Location $Target
try {
    $gitArgs = @('apply', '--verbose', '--whitespace=nowarn')
    if ($Revert)    { $gitArgs += '-R' }
    if ($CheckOnly) { $gitArgs += '--check' }
    if ($Reject)    { $gitArgs += '--reject' }
    if ($already -or $Revert) { $gitArgs += '--3way' }
    $gitArgs += $patch
    Write-Host "> git $($gitArgs -join ' ')" -ForegroundColor DarkGray
    & git @gitArgs
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($code -ne 0) {
    if ($Reject) {
        $rej = Get-ChildItem $Target -Recurse -Filter '*.rej' -ErrorAction SilentlyContinue
        Write-Host ""
        Write-Host "[!] partial apply: $($rej.Count) hunk(s) rejected and written to .rej files." -ForegroundColor Yellow
        foreach ($r in $rej) { Write-Host "    $($r.FullName.Replace($Target + '\', ''))" -ForegroundColor Yellow }
        Write-Host "    Fix them by hand using the hunk context in patch\ddnet-background.patch, then delete the .rej files." -ForegroundColor Yellow
        exit 0
    }
    Write-Host ""
    Write-Host "[x] git apply failed (exit $code)." -ForegroundColor Red
    Write-Host "    Common causes: the tree is not based on TClient 10.9.0 (commit 6b4118bf0), or it already" -ForegroundColor Red
    Write-Host "    has local modifications in the touched files. Re-run with -Reject to see which hunks clash." -ForegroundColor Red
    exit $code
}

if ($CheckOnly) { Write-Host "[ok] patch applies cleanly." -ForegroundColor Green; exit 0 }
if ($Revert)    { Write-Host "[ok] feature removed (files deleted, hooks reverted)." -ForegroundColor Green; exit 0 }

Write-Host "[ok] background module installed into $Target" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps" -ForegroundColor Cyan
Write-Host "  1) FFmpeg (video only): replace the ddnet-libs FFmpeg DLLs with the BtbN 8.1 shared build"
Write-Host "     (avcodec-62, avformat-62, avutil-60, swresample-6, swscale-9); cmake/FindFFMPEG.cmake is already"
Write-Host "     patched to those names. Without it, images work but video files cannot be opened."
Write-Host "  2) Build:  cmake -A x64 -DVULKAN=OFF -DDOWNLOAD_GTEST=OFF .   then   cmake --build . --config Release --target game-client"
Write-Host "  3) Run, then: Settings -> Background, and put your files into <save dir>\Background"
Write-Host "     (Windows save dir: %APPDATA%\DDNet)"
