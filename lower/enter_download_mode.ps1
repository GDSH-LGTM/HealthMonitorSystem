<#
.SYNOPSIS
    Manually pulse the ESP32-S3 "download mode" reset sequence over DTR/RTS
    (the software equivalent of holding BOOT and tapping RESET).

.DESCRIPTION
    The ESP32 auto-download circuit drives EN(RESET) and GPIO0(BOOT) from DTR/RTS:
        DTR = RESET (EN, active low)     RTS = BOOT (GPIO0, active low)
    Sequence (same idea as esptool's, but slowed down):
        DTR=1 RTS=1  ->  RTS=0 (pull BOOT)  ->  DTR=0 (pulse RESET)
        ->  DTR=1 (release RESET)  ->  RTS=1 (release BOOT)

    *** This does NOT work on this board. ***
    All 45 combinations tested by find_download_timing.ps1 - 5 wiring families x
    9 timings - still come up as boot:0xc (GPIO0 high = normal boot), so the
    board's DTR/RTS are simply not wired to EN/GPIO0. The script is kept because
    it is harmless and would work on a board that does have the circuit; for
    this one you must press the buttons by hand (see bringup.ps1).

.NOTES
    ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 files as GBK on a
    zh-CN system, which garbles non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$PulseMs = 120
)

$ErrorActionPreference = "Stop"

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, "One"
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.Open()

Start-Sleep -Milliseconds 200

# 1) release both
$sp.DtrEnable = $true
$sp.RtsEnable = $true
Start-Sleep -Milliseconds $PulseMs

# 2) pull BOOT low (GPIO0 = 0)
$sp.RtsEnable = $false
Start-Sleep -Milliseconds $PulseMs

# 3) pulse RESET low while BOOT stays low -> latches download mode
$sp.DtrEnable = $false
Start-Sleep -Milliseconds $PulseMs

# 4) release RESET, keep BOOT low
$sp.DtrEnable = $true
Start-Sleep -Milliseconds $PulseMs

# 5) release BOOT
$sp.RtsEnable = $true
Start-Sleep -Milliseconds $PulseMs

$sp.Close()

Write-Host "Sent the download-mode reset sequence to $Port (BOOT+RESET via DTR/RTS)." -ForegroundColor Green
Write-Host "NOTE: this board ignores it - its DTR/RTS are not wired to EN/GPIO0." -ForegroundColor Yellow
Write-Host "Press the buttons by hand instead: hold BOOT, tap RESET, release BOOT," -ForegroundColor Yellow
Write-Host "then run .\bringup.ps1 (or .\flash_manual.ps1) immediately." -ForegroundColor Yellow
exit 0
