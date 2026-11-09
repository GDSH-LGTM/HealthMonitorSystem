<#
.SYNOPSIS
    Flash immediately when you press Enter - no countdown, no timing race.

.DESCRIPTION
    Why this exists:
      The countdown-based scripts fought the ROM's download window. The ESP32-S3
      ROM samples GPIO0 at the reset instant and then only waits briefly for a
      sync before falling back to normal boot. So the button combo has to happen
      just BEFORE esptool starts - not 12 seconds before, and not after.

      The evidence from an actual run: 5 attempts where the combo was pressed at
      the end of the countdown all failed with "No serial data received",
      while the one attempt pressed mid-countdown got a response ("Invalid head
      of packet"). The window is real but narrow, and an interactive trigger
      removes the guesswork entirely.

    Usage:
      1. run this script
      2. HOLD BOOT  ->  TAP RESET  ->  RELEASE BOOT
      3. immediately press Enter

    esptool is then launched with --before no_reset, i.e. it talks to a chip that
    is already sitting in download mode, waiting.

      .\flash_now.ps1                  # 115200
      .\flash_now.ps1 -Baud 74880      # if you saw corruption errors
      .\flash_now.ps1 -Rounds 3        # re-prompt up to 3 times

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 files as GBK
    on a zh-CN system, which corrupts non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$Baud = 115200,
    [int]$Rounds = 4
)

$ErrorActionPreference = "Continue"
$lower = $PSScriptRoot
Set-Location $lower

$py  = "D:\ESP_IDF\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$esp = "D:\ESP_IDF\Espressif\frameworks\esp-idf-v5.5.5\components\esptool_py\esptool\esptool.py"

if (-not (Test-Path $py))  { throw "idf python not found: $py" }
if (-not (Test-Path $esp)) { throw "esptool not found: $esp" }

$images = @(
    @{ off = "0x0";     file = "build\bootloader\bootloader.bin" },
    @{ off = "0x8000";  file = "build\partition_table\partition-table.bin" },
    @{ off = "0x10000"; file = "build\ota_data_initial.bin" },
    @{ off = "0x20000"; file = "build\health_monitor_band.bin" }
)
foreach ($i in $images) {
    if (-not (Test-Path $i.file)) { throw "missing $($i.file) - build first" }
}

$flashArgs = @(
    "--chip", "esp32s3",
    "-p", $Port,
    "-b", "$Baud",
    "--before", "no_reset",
    "--after", "hard_reset",
    "--connect-attempts", "20",
    "write_flash",
    "--flash_mode", "dio",
    "--flash_freq", "80m",
    "--flash_size", "16MB"
)
foreach ($i in $images) { $flashArgs += @($i.off, $i.file) }

Write-Host ""
Write-Host ("=" * 68) -ForegroundColor Cyan
Write-Host " FLASH NOW - interactive, no countdown" -ForegroundColor Cyan
Write-Host ("=" * 68) -ForegroundColor Cyan
Write-Host ""
Write-Host " The chip must ALREADY be in download mode when esptool starts." -ForegroundColor Yellow
Write-Host " So do the combo FIRST, then press Enter immediately:" -ForegroundColor Yellow
Write-Host ""
Write-Host "   1. HOLD BOOT" -ForegroundColor White
Write-Host "   2. TAP RESET" -ForegroundColor White
Write-Host "   3. RELEASE BOOT" -ForegroundColor White
Write-Host "   4. press Enter HERE, straight away" -ForegroundColor White
Write-Host ""
Write-Host " Do not wait - the ROM only holds the download window briefly." -ForegroundColor Gray
Write-Host ""

$done = $false
for ($r = 1; $r -le $Rounds; $r++) {
    Write-Host ("--- round {0} / {1}   baud {2} ---" -f $r, $Rounds, $Baud) -ForegroundColor Cyan
    Write-Host "  do the combo, then press Enter... " -NoNewline -ForegroundColor White
    $null = Read-Host

    Write-Host "  flashing ..."
    $out = & $py $esp @flashArgs 2>&1
    $code = $LASTEXITCODE

    $out | Select-String -Pattern "Chip is|MAC:|Wrote|Hash of data verified|Leaving|fatal error|Failed to connect|Invalid head|Wrong boot" |
        Select-Object -Last 10 | ForEach-Object { "    " + $_.Line.Trim() }

    if ($code -eq 0) { $done = $true; break }

    Write-Host ""
    if ($out -match "Invalid head of packet") {
        Write-Host "  -> chip WAS in download mode, link corrupted." -ForegroundColor Yellow
        Write-Host "     retry; if it repeats, drop the baud:" -ForegroundColor Yellow
        Write-Host "       .\flash_now.ps1 -Baud 74880" -ForegroundColor Yellow
    } elseif ($out -match "No serial data received") {
        Write-Host "  -> nothing heard: the chip had already left download mode." -ForegroundColor Yellow
        Write-Host "     press Enter FASTER after releasing BOOT." -ForegroundColor Yellow
    } elseif ($out -match "Wrong boot mode") {
        Write-Host "  -> chip booted normally: BOOT was not held while RESET was tapped." -ForegroundColor Yellow
        Write-Host "     hold BOOT DOWN, tap RESET, only then release BOOT." -ForegroundColor Yellow
    }
    Write-Host ""
}

Write-Host ""
if ($done) {
    Write-Host ("=" * 68) -ForegroundColor Green
    Write-Host " FLASH OK" -ForegroundColor Green
    Write-Host ("=" * 68) -ForegroundColor Green
    Write-Host ""
    Write-Host " Capture the boot log now:" -ForegroundColor Cyan
    Write-Host "   .\bringup.ps1 -SkipBuild -SkipFlash -CaptureSeconds 25" -ForegroundColor Cyan
    exit 0
} else {
    Write-Host ("=" * 68) -ForegroundColor Red
    Write-Host " still failing after $Rounds rounds" -ForegroundColor Red
    Write-Host ("=" * 68) -ForegroundColor Red
    Write-Host ""
    Write-Host " Please report:" -ForegroundColor Yellow
    Write-Host "   - the button silkscreen on your board (is there a BOOT/IO0 button?)" -ForegroundColor Yellow
    Write-Host "   - whether the USB cable is a data cable and firmly seated" -ForegroundColor Yellow
    Write-Host "   - the exact error lines above" -ForegroundColor Yellow
    exit 1
}
