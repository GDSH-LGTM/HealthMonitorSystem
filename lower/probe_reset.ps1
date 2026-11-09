<#
.SYNOPSIS
    Non-interactive reset probe: reset the board programmatically, then check
    whether BOOT (GPIO0) was low by reading the ROM boot mode.

.DESCRIPTION
    The ESP32-S3 ROM prints a boot line right after reset:

        ESP-ROM:esp32s3-20210327 | ... | rst:0x1 (POWERON),boot:0xc (SPI_FAST_FLASH_BOOT)

    boot:0xc -> GPIO0 high -> normal boot
    boot:0x0 -> GPIO0 low  -> download mode

    So: hold BOOT by hand, let this script pulse RESET, and the script reports
    which mode the chip came up in. That tells us definitively whether the
    BOOT button is wired to GPIO0, independent of DTR/RTS.

    Run it, then follow the on-screen prompt. You have 15 seconds.

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 as GBK on a
    zh-CN system and would corrupt non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$WaitSeconds = 15
)

$ErrorActionPreference = "Continue"

Write-Host ""
Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host " READY? Then:" -ForegroundColor Yellow
Write-Host ""
Write-Host "   1. PRESS AND HOLD the BOOT button  (keep holding!)" -ForegroundColor White
Write-Host "   2. Do NOT release it for the next $WaitSeconds seconds" -ForegroundColor White
Write-Host ""
Write-Host " This script will pulse RESET while you hold BOOT." -ForegroundColor Gray
Write-Host " Releasing BOOT too early is the most common failure." -ForegroundColor Gray
Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host ""

for ($t = $WaitSeconds; $t -gt 0; $t--) {
    Write-Host ("`r  pulsing RESET in {0,2} s ... (keep holding BOOT)  " -f $t) -NoNewline
    Start-Sleep -Seconds 1
}
Write-Host "`r  pulsing RESET NOW - keep holding BOOT!            "

# Open the port, assert BOOT via RTS, pulse RESET via DTR, then read.
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, "One"
try {
    $sp.Open()
} catch {
    Write-Host "cannot open $Port : $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$sp.DtrEnable = $true
$sp.RtsEnable = $true
Start-Sleep -Milliseconds 150
$sp.RtsEnable = $false      # pretend to pull BOOT low (may do nothing on this board)
Start-Sleep -Milliseconds 150
$sp.DtrEnable = $false      # pulse RESET low
Start-Sleep -Milliseconds 150
$sp.DtrEnable = $true       # release RESET; your finger still holds BOOT
Start-Sleep -Milliseconds 100
$sp.RtsEnable = $true       # release our BOOT attempt; your finger still holds it

Write-Host "  now RELEASE the BOOT button" -ForegroundColor Yellow

$sp.ReadTimeout = 2500
Start-Sleep -Milliseconds 300
$data = ""
try { $data = $sp.ReadExisting() } catch { }
$sp.Close()

$clean = ($data -replace "`e\[[0-9;]*m", "") -replace "[^\x20-\x7E\r\n]", "."

Write-Host ""
if ($clean -match "boot:(0x[0-9a-fA-F]+)") {
    $boot = $Matches[1]
    Write-Host "ROM boot mode: boot=$boot" -ForegroundColor Cyan
    if ($boot -match "^0x0+$") {
        Write-Host ""
        Write-Host "*** DOWNLOAD MODE - the BOOT button IS wired to GPIO0 ***" -ForegroundColor Green
        Write-Host "Run the flash now while the chip stays in this mode:" -ForegroundColor Green
        Write-Host "  .\flash_manual.ps1 -Port $Port -Baud 460800 -WaitSeconds 3" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "Normal boot (GPIO0 high). The BOOT button is NOT reaching GPIO0," -ForegroundColor Yellow
        Write-Host "or it was released before RESET came out." -ForegroundColor Yellow
        Write-Host "Look at the board: is there really a BOOT/IO0 button, or only RESET/EN?" -ForegroundColor Yellow
    }
} elseif ($data.Length -gt 0) {
    Write-Host "No ROM boot line, but got $($data.Length) bytes:" -ForegroundColor Yellow
    ($clean -split "`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ } | Select-Object -First 6) |
        ForEach-Object { Write-Host "  $_" }
} else {
    Write-Host "Completely silent - the chip did not boot and did not respond." -ForegroundColor Yellow
    Write-Host "That usually means it IS in download mode but never got a sync." -ForegroundColor Yellow
    Write-Host "Try flashing immediately with -WaitSeconds 1:" -ForegroundColor Yellow
    Write-Host "  .\flash_manual.ps1 -Port $Port -Baud 115200 -WaitSeconds 1" -ForegroundColor Yellow
}
exit 0
