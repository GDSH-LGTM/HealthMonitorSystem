<#
.SYNOPSIS
    Switch the upper computer's EFFECTIVE config between simulated source and
    the real ESP32-S3 band on COM15.

.DESCRIPTION
    Why this exists:
      The upper computer reads ./appconfig.ini relative to its working directory,
      i.e. the copy in the BUILD directory - not the one in the source tree.
      But CMake's POST_BUILD step does copy_if_different from the source template
      into the build directory on every build. The shipped template has
      useSimulatedSource=1 and bandPortName=COM3, so:
        - in the Qt app, settings-page edits ARE persisted to the build copy
          (they survive a restart), but
        - any REBUILD quietly resets them back to simulation mode.
      That trap already cost the previous owner a smoke-test failure.

    So: run this AFTER building, whenever you want the real board instead of the
    simulator. It only touches the build-directory copy, never the template, so a
    rebuild legitimately restores simulation mode for hardware-free testing.

    Only the values being changed are substituted - the rest of the file is left
    byte-identical. In particular it writes UTF-8 WITHOUT a BOM: PowerShell's
    -Encoding utf8 adds one, and a BOM in front of "[System]" risks breaking the
    QSettings parse of the very first section.

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 as GBK on a
    zh-CN system, which corrupts non-ASCII text and breaks parsing.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [string]$PcIp = "",
    [switch]$Simulated,
    [switch]$Show
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $repoRoot "build-HealthMonitorSystem-Desktop_Qt_5_14_2_MinGW_64_bit-Debug"
$ini      = Join-Path $buildDir "appconfig.ini"
$template = Join-Path $repoRoot "HealthMonitorSystem\config\appconfig.ini"

if (-not (Test-Path $ini)) {
    throw "not found: $ini - build the upper computer first (Qt Creator, or cmake --build)"
}

# UTF-8 without BOM, and no newline translation.
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Read-Ini  { [System.IO.File]::ReadAllText($ini, [System.Text.Encoding]::UTF8) }
function Write-Ini($text) { [System.IO.File]::WriteAllText($ini, $text, $Utf8NoBom) }

function Show-Current {
    Write-Host ""
    Write-Host "--- effective config ---" -ForegroundColor Cyan
    Write-Host "  $ini" -ForegroundColor DarkGray
    $t = Read-Ini
    ($t -split "`r?`n") | Where-Object { $_ -match '^\s*[^;#\[]' -or $_ -match '^\[' } |
        ForEach-Object { "  " + $_.Trim() }
    Write-Host ""
}

if ($Show) { Show-Current; exit 0 }

$c = Read-Ini

# Two traps this regex has to avoid, both of which silently corrupted the file
# in earlier versions:
#   1. \s matches newlines. "wifiPcIp\s*=" therefore swallowed the line break
#      plus the start of the NEXT line, so substituting it deleted imageSavePath.
#      Horizontal space only -> [ \t]*.
#   2. A consuming [^\r\n]* eats the terminator. Use a lookahead instead, so the
#      line ending and everything after it stay byte-identical.
$Eol = '(?=[\r\n]|$)'
$Sp  = '[ \t]*'

if ($Simulated) {
    # Restore exactly what the shipped template has, so simulation mode looks
    # like a fresh build and nothing surprising is left behind.
    $c = $c -replace "useSimulatedSource$Sp=$Sp\d+$Eol", 'useSimulatedSource=1'
    $c = $c -replace "wifiPcIp$Sp=$Sp[^\r\n]*$Eol",      'wifiPcIp='
    $c = $c -replace "bandPortName$Sp=$Sp[^\r\n]*$Eol",  'bandPortName=COM3'
    Write-Ini $c
    Write-Host "switched to SIMULATED source (no hardware needed)" -ForegroundColor Green
    Show-Current
    exit 0
}

# --- real hardware ---
$c = $c -replace "useSimulatedSource$Sp=$Sp\d+$Eol", 'useSimulatedSource=0'
$c = $c -replace "bandPortName$Sp=$Sp[^\r\n]*$Eol",  "bandPortName=$Port"
if ($PcIp) {
    $c = $c -replace "wifiPcIp$Sp=$Sp[^\r\n]*$Eol", "wifiPcIp=$PcIp"
}
Write-Ini $c

Write-Host "switched to REAL HARDWARE (band on $Port)" -ForegroundColor Green
if (-not $PcIp) {
    Write-Host "  wifiPcIp left empty -> the app auto-picks the first non-loopback IPv4." -ForegroundColor Gray
    Write-Host "  This machine also has VMware and virtual adapters, so that auto-pick can" -ForegroundColor Yellow
    Write-Host "  choose the wrong address. Pin it explicitly:" -ForegroundColor Yellow
    Write-Host "    .\use_real_band.ps1 -PcIp 192.168.43.190" -ForegroundColor Yellow
}
Show-Current

Write-Host "Reminders:" -ForegroundColor Cyan
Write-Host "  1. a REBUILD resets this file back to the template (simulation mode)" -ForegroundColor Yellow
Write-Host "  2. COM15 must not be held by idf.py monitor while the app is running" -ForegroundColor Yellow
Write-Host "  3. Windows firewall must allow inbound TCP 45678 (image channel)" -ForegroundColor Yellow
Write-Host ""
Write-Host "Template left untouched: $template" -ForegroundColor DarkGray
exit 0
