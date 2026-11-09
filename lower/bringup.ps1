<#
.SYNOPSIS
    One-shot band bring-up: build (optional) -> flash -> capture boot log -> analyze.

.DESCRIPTION
    Exists because this board cannot be put into download mode by software: all
    45 DTR/RTS timing combinations across 5 wiring families still boot with
    boot:0xc (GPIO0 high), and all three esptool --before modes fail. So the
    BOOT button has to be pressed by hand, and the fewer round trips that costs
    the better.

    This script does everything in ONE run:
      1. optional build
      2. countdown while you do: HOLD BOOT -> tap RESET -> release BOOT
      3. flash (with retries, so a missed window is recoverable)
      4. immediately capture the serial log and pull out the answers we need:
           - does PSRAM exist            (decides camera frame buffer placement)
           - are both I2C sensors present (0x44 SHT30 / 0x57 MAX30102)
           - does SHT30 return real values
           - is the MAX30102 10ms sampler running without FIFO backlog
           - did WiFi connect, and the image channel come up
      5. write the full log to bringup_log.txt

    Usage:
      .\bringup.ps1                     # flash + capture (default)
      .\bringup.ps1 -SkipBuild          # skip the build step
      .\bringup.ps1 -BuildOnly
      .\bringup.ps1 -WaitSeconds 90     # more time for the button dance
      .\bringup.ps1 -Baud 115200        # if 460800 keeps failing

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 as GBK on a
    zh-CN system and would corrupt non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$Baud = 460800,
    [int]$WaitSeconds = 45,
    [int]$CaptureSeconds = 20,
    [int]$Retries = 3,
    [switch]$SkipBuild,
    [switch]$BuildOnly,
    [switch]$SkipFlash
)

$ErrorActionPreference = "Continue"
$lower = $PSScriptRoot
Set-Location $lower

$py  = "D:\ESP_IDF\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$esp = "D:\ESP_IDF\Espressif\frameworks\esp-idf-v5.5.5\components\esptool_py\esptool\esptool.py"
$logFile = Join-Path $lower "bringup_log.txt"

function Write-Head($t) {
    Write-Host ""
    Write-Host ("=" * 66) -ForegroundColor Cyan
    Write-Host "  $t" -ForegroundColor Cyan
    Write-Host ("=" * 66) -ForegroundColor Cyan
}

# ---------------------------------------------------------------- 1. build
$images = @(
    @{ off = "0x0";     file = "build\bootloader\bootloader.bin" },
    @{ off = "0x8000";  file = "build\partition_table\partition-table.bin" },
    @{ off = "0x10000"; file = "build\ota_data_initial.bin" },
    @{ off = "0x20000"; file = "build\health_monitor_band.bin" }
)

if (-not $SkipBuild) {
    Write-Head "STEP 1/4  build"
    & (Join-Path $lower "flash.ps1") -Port $Port -Baud $Baud build 2>&1 |
        Select-String -Pattern "error:|fatal error|FAILED:|binary size|Project build complete" |
        Select-Object -Last 8 | ForEach-Object { "  " + $_.Line.Trim() }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "BUILD FAILED - fix that first." -ForegroundColor Red
        exit 1
    }
} else {
    Write-Head "STEP 1/4  build (skipped)"
}

foreach ($i in $images) {
    if (-not (Test-Path $i.file)) {
        Write-Host "missing image: $($i.file) - run without -SkipBuild" -ForegroundColor Red
        exit 1
    }
}

if ($BuildOnly) {
    Write-Host "build only, done." -ForegroundColor Green
    exit 0
}

# ---------------------------------------------------------------- 2. buttons
if (-not $SkipFlash) {
    Write-Head "STEP 2/4  put the board into DOWNLOAD MODE"
    Write-Host ""
    Write-Host "  The board's DTR/RTS are NOT wired to EN/GPIO0, so this cannot be" -ForegroundColor Yellow
    Write-Host "  done in software. Do it by hand:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "    1. HOLD the BOOT button  (keep holding it!)" -ForegroundColor White
    Write-Host "    2. TAP the RESET button once" -ForegroundColor White
    Write-Host "    3. RELEASE the BOOT button" -ForegroundColor White
    Write-Host ""
    Write-Host "  Releasing BOOT before RESET is out is the most common failure." -ForegroundColor Gray
    Write-Host "  If this board has no BOOT button, short GPIO0 to GND instead of step 1." -ForegroundColor Gray
    Write-Host ""

    for ($t = $WaitSeconds; $t -gt 0; $t--) {
        Write-Host ("`r  flashing in {0,3} s ...   " -f $t) -NoNewline
        Start-Sleep -Seconds 1
    }
    Write-Host "`r  go!                              "

    # ------------------------------------------------------------ 3. flash
    Write-Head "STEP 3/4  flash"
    $flashArgs = @(
        "--chip", "esp32s3", "-p", $Port, "-b", "$Baud",
        "--before", "no_reset", "--after", "hard_reset",
        "write_flash", "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "16MB"
    )
    foreach ($i in $images) { $flashArgs += @($i.off, $i.file) }

    $flashed = $false
    for ($attempt = 1; $attempt -le $Retries; $attempt++) {
        Write-Host "  --- attempt $attempt / $Retries ---" -ForegroundColor DarkGray
        $out = & $py $esp @flashArgs 2>&1
        $code = $LASTEXITCODE
        $out | Select-String -Pattern "Chip is|MAC:|Wrote|Hash of data verified|Leaving|fatal error|Failed to connect|Wrong boot" |
            Select-Object -Last 6 | ForEach-Object { "    " + $_.Line.Trim() }

        if ($code -eq 0) { $flashed = $true; break }

        if ($attempt -lt $Retries) {
            Write-Host "  failed. Re-do the button combo:" -ForegroundColor Yellow
            Write-Host "    HOLD BOOT -> tap RESET -> release BOOT" -ForegroundColor Yellow
            for ($t = 20; $t -gt 0; $t--) {
                Write-Host ("`r    retrying in {0,2} s ...  " -f $t) -NoNewline
                Start-Sleep -Seconds 1
            }
            Write-Host "`r    go!                        "
        }
    }

    if (-not $flashed) {
        Write-Host ""
        Write-Host "FLASH FAILED after $Retries attempts." -ForegroundColor Red
        Write-Host "Try: .\bringup.ps1 -Baud 115200 -WaitSeconds 90" -ForegroundColor Yellow
        Write-Host "Or run .\probe_reset.ps1 to check whether BOOT actually reaches GPIO0." -ForegroundColor Yellow
        exit 1
    }
    Write-Host "  FLASH OK" -ForegroundColor Green
} else {
    Write-Head "STEP 2-3/4  flash (skipped)"
}

# ---------------------------------------------------------------- 4. capture
Write-Head "STEP 4/4  capture boot log ($CaptureSeconds s)"
Write-Host "  (the chip just hard-reset, so the log should start at the ROM banner)"

$all = ""
try {
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, "One"
    $sp.ReadTimeout = 1000
    $sp.Open()
    $deadline = (Get-Date).AddSeconds($CaptureSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $chunk = $sp.ReadExisting()
            if ($chunk.Length -gt 0) { $all += $chunk }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    $sp.Close()
} catch {
    Write-Host "  cannot read $Port : $($_.Exception.Message)" -ForegroundColor Red
}

$clean = ($all -replace "`e\[[0-9;]*m", "") -replace "[^\x20-\x7E\r\n]", ""
$clean | Out-File $logFile -Encoding utf8
Write-Host "  captured $($all.Length) bytes -> $logFile"
if ($all.Length -eq 0) {
    Write-Host "  NOTHING RECEIVED. The board may not have rebooted, or COM port is wrong." -ForegroundColor Yellow
    exit 1
}

# ---------------------------------------------------------------- analysis
Write-Head "ANALYSIS"

# First decide WHICH firmware is on the board. The old firmware does not print
# PSRAM/I2C lines at all, so running the rest of the checks against it would
# produce a page of misleading "NOT FOUND" verdicts.
$isOld = ($clean -match "SENSORS: WiFi disconnected, retrying")

if ($isOld) {
    Write-Host ""
    Write-Host "  *** THE OLD FIRMWARE IS STILL ON THE BOARD ***" -ForegroundColor Red
    Write-Host ""
    Write-Host "  The log shows 'SENSORS: WiFi disconnected, retrying', which only the" -ForegroundColor Yellow
    Write-Host "  old build prints. So the flash did not take effect - either the BOOT" -ForegroundColor Yellow
    Write-Host "  button did not actually drop GPIO0, or the write failed silently." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  The other checks below would be meaningless (the old firmware never" -ForegroundColor Gray
    Write-Host "  prints PSRAM or I2C scan lines), so they are skipped." -ForegroundColor Gray
    Write-Host ""
    Write-Host "  Next: run .\probe_reset.ps1 to find out whether BOOT reaches GPIO0," -ForegroundColor Cyan
    Write-Host "  then retry with:  .\bringup.ps1 -Baud 115200 -WaitSeconds 90" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Full log: $logFile" -ForegroundColor DarkGray
    exit 2
}

function Show-Verdict($label, $ok, $detail) {
    $mark = if ($ok) { "[OK]  " } else { "[!!]  " }
    $color = if ($ok) { "Green" } else { "Yellow" }
    Write-Host ("  {0}{1,-34} {2}" -f $mark, $label, $detail) -ForegroundColor $color
}

# PSRAM - the open question that decides camera frame buffer placement
$psramLine = ($clean -split "`n" | Where-Object { $_ -match "PSRAM" } | Select-Object -First 2)
if ($psramLine -match "PSRAM: (\d+)") {
    $bytes = [int]$Matches[1]
    Show-Verdict "PSRAM" ($bytes -gt 0) "$bytes bytes  (camera frame buffers go to $(if($bytes -gt 0){'PSRAM, fb_count=2'}else{'DRAM, fb_count=1'}))"
} elseif ($psramLine) {
    Show-Verdict "PSRAM" $false ($psramLine -join " | ").Trim()
} else {
    Show-Verdict "PSRAM" $false "no PSRAM line in the log - check the boot report"
}

# I2C sensors
$sht30 = ($clean -match "0x44")
$max   = ($clean -match "0x57")
Show-Verdict "SHT30  (0x44)"  $sht30 $(if ($sht30) { "found on I2C" } else { "NOT FOUND - check wiring and 4.7k pull-ups" })
Show-Verdict "MAX30102 (0x57)" $max  $(if ($max)   { "found on I2C" } else { "NOT FOUND - check wiring and 4.7k pull-ups" })

# Sensor readings
$hrLine = ($clean -split "`n" | Where-Object { $_ -match "HR=.*SpO2=" } | Select-Object -Last 1)
Show-Verdict "MAX30102 readings" ([bool]$hrLine) $(if ($hrLine) { $hrLine.Trim() } else { "no HR/SpO2 line yet (needs finger on the sensor for ~5 s)" })

$shtErr = ($clean -match "SHT30 .*failed|SHT30 CRC")
Show-Verdict "SHT30 reads" (-not $shtErr) $(if ($shtErr) { "read failure logged - see log" } else { "no failure logged" })

# FIFO backlog - the bug the rewrite was meant to fix
$backlog = ($clean -split "`n" | Where-Object { $_ -match "FIFO" } | Measure-Object).Count
Show-Verdict "MAX30102 FIFO backlog" ($backlog -eq 0) $(if ($backlog -eq 0) { "none" } else { "$backlog occurrence(s) - the 10 ms sampler is being starved" })

# uart driver error - the old firmware spammed this
$uartErr = ($clean -match "uart driver error")
Show-Verdict "uart driver error" (-not $uartErr) $(if ($uartErr) { "STILL PRESENT - uart_driver_install did not take effect" } else { "gone (was the old firmware's problem)" })

# WiFi + image channel
$ip = ($clean -split "`n" | Where-Object { $_ -match "IP:" } | Select-Object -Last 1)
Show-Verdict "WiFi" ([bool]$ip) $(if ($ip) { $ip.Trim() } else { "no IP yet - check SSID/password in app_config.h" })

$imgUp = ($clean -match "image channel|image: connected")
Show-Verdict "image channel (0x09/TCP)" $imgUp $(if ($imgUp) { "up" } else { "not up - needs the PC listening on 45678" })

Write-Host ""
Write-Host "Full log: $logFile" -ForegroundColor DarkGray
Write-Host "Send me that log (or just the ANALYSIS block above) and I will continue." -ForegroundColor Cyan
exit 0
