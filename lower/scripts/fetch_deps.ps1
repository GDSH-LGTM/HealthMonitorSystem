<#
.SYNOPSIS
    Make the third-party ESP-IDF components available under third_party/.

.DESCRIPTION
    These are deliberately NOT tracked in git. They come from the archived copy
    of the old firmware, which already had a complete managed_components
    checkout, so no network access is needed.

    They are wired in via EXTRA_COMPONENT_DIRS in the top-level CMakeLists.txt,
    so their component names stay normal ('lvgl', 'esp32_camera', 'esp_jpeg').

      lvgl           8.4.0    UI
      esp32-camera   2.1.8    OV2640 driver
      esp-jpeg       1.3.1    needed by esp32-camera

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 as GBK on a
    zh-CN system and would corrupt non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Archive = "",
    [string[]]$Only = @(),
    [switch]$Force
)

$ErrorActionPreference = "Stop"

# Repo root is the PARENT of this script's folder (script lives in <root>\scripts).
$repoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $repoRoot

if (-not $Archive) {
    $Archive = Join-Path (Split-Path $repoRoot -Parent) `
        "lower_old_archive\HealthMonitorSystem\firmware\esp32\managed_components"
}

# source dir name under managed_components  ->  destination dir under third_party
# NOTE: the destination name IS the component name ESP-IDF will use, so
# esp32_camera is spelled with an underscore (upstream ships esp32-camera).
$map = [ordered]@{
    "lvgl__lvgl"                 = "lvgl"
    "espressif__esp32-camera"    = "esp32_camera"
}

if (-not (Test-Path $Archive)) {
    Write-Host "archive not found: $Archive" -ForegroundColor Red
    Write-Host ""
    Write-Host "Alternative: let the IDF component manager fetch them (needs network):" -ForegroundColor Yellow
    Write-Host "  .\flash.ps1 add-dependency lvgl/lvgl^8.4.0" -ForegroundColor Yellow
    Write-Host "  .\flash.ps1 add-dependency espressif/esp32-camera^2.1.8" -ForegroundColor Yellow
    exit 1
}

$tpRoot = Join-Path $repoRoot "third_party"
New-Item -ItemType Directory -Path $tpRoot -Force | Out-Null

foreach ($srcName in $map.Keys) {
    $dstName = $map[$srcName]
    if ($Only.Count -gt 0 -and $Only -notcontains $dstName) { continue }

    $src = Join-Path $Archive $srcName
    $dst = Join-Path $tpRoot $dstName

    if (-not (Test-Path $src)) {
        Write-Host "skip (missing in archive): $srcName" -ForegroundColor Yellow
        continue
    }
    if (Test-Path $dst) {
        if (-not $Force) {
            Write-Host "already present: third_party\$dstName" -ForegroundColor Green
            continue
        }
        Remove-Item $dst -Recurse -Force
    }

    Copy-Item $src $dst -Recurse -Force
    Remove-Item (Join-Path $dst ".git") -Recurse -Force -ErrorAction SilentlyContinue

    $n  = (Get-ChildItem $dst -Recurse -File | Measure-Object).Count
    $sz = (Get-ChildItem $dst -Recurse -File | Measure-Object -Property Length -Sum).Sum
    Write-Host ("ready: third_party\{0}  ({1} files, {2:N1} MB)" -f $dstName, $n, ($sz / 1MB)) -ForegroundColor Green
}

Write-Host ""
Write-Host "third_party contents:" -ForegroundColor Cyan
Get-ChildItem $tpRoot -Directory | ForEach-Object { Write-Host "  $($_.Name)" }
exit 0
