<#
.SYNOPSIS
    Flash over the ESP32-S3's USB Serial/JTAG controller (USB-C port) instead of
    UART0 (Micro-USB / CH343).

.DESCRIPTION
    Why this is the right path for this board:

      UART0 download on this setup is unreliable. We swept the baud rate and the
      corruption is present at EVERY rate, with a different garbage byte each
      time:
          9600   -> Invalid head of packet (0x66)
          57600  -> Invalid head of packet (0xC6)
          115200 -> Wrong boot mode (0x4)   [chip booted the app]
          74880  -> Invalid head of packet (0xA8)
      Corruption at 9600 rules out baud/framing mismatch. Normal UART traffic
      works fine (the old firmware's logs come through cleanly at 115200), so it
      is specifically the ROM's download handshake that is flaky on this link -
      most likely wiring/signal integrity on the Micro-USB path.

      The ESP32-S3 has a SECOND download path that does not use UART0 at all:
      the built-in USB Serial/JTAG controller on GPIO19/20, reached through the
      board's USB-C port. Per the ESP-IDF docs it "is able to put the chip into
      download mode automatically" - no BOOT button, no CH343, no baud games,
      and it is a completely different USB peripheral.

    HARDWARE PREREQUISITE - READ THIS:

      Our firmware uses GPIO19 and GPIO20 for the I2C bus (MAX30102 + SHT30),
      and those are the SAME pins as the native USB D-/D+.

      So before using this path you must DISCONNECT the two I2C wires
      (or whatever is wired to GPIO19/20), otherwise USB cannot enumerate.

      The sensors can go back on afterwards; only flashing needs them off.

    STEPS:
      1. unplug the Micro-USB cable
      2. disconnect whatever is wired to GPIO19 / GPIO20 (I2C: MAX30102 + SHT30)
      3. plug the USB-C cable into the PC
      4. run this script - it finds the port and flashes

    Usage:
      .\flash_usb.ps1                      # auto-detect the USB-JTAG port
      .\flash_usb.ps1 -ListOnly            # just show candidate ports
      .\flash_usb.ps1 -Port COM20          # specify it yourself

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 files as GBK
    on a zh-CN system, which corrupts non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "",
    [int]$Baud = 460800,
    [switch]$ListOnly
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

function Show-Ports {
    Write-Host ""
    Write-Host "Serial ports and their USB IDs:" -ForegroundColor Cyan
    $found = @()
    Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '\(COM\d+\)' } |
        ForEach-Object {
            $name = $_.Name
            $id   = $_.DeviceID
            # Espressif USB Serial/JTAG enumerates with VID 303A
            $isEspressif = $id -match 'VID_303A'
            $tag = if ($isEspressif) { "  <== ESP32-S3 USB Serial/JTAG" } else { "" }
            Write-Host ("  {0}{1}" -f $name, $tag) -ForegroundColor $(if ($isEspressif) { "Green" } else { "Gray" })
            if ($isEspressif) { $found += $name }
        }
    Write-Host ""
    if ($found.Count -eq 0) {
        Write-Host "No Espressif USB device (VID_303A) found." -ForegroundColor Yellow
        Write-Host "That means the USB-C port is not connected, or GPIO19/20 are still" -ForegroundColor Yellow
        Write-Host "held by the I2C wiring so USB cannot enumerate." -ForegroundColor Yellow
    }
    return $found
}

# Auto-detect: an Espressif USB Serial/JTAG device, if present.
if (-not $Port) {
    $espPorts = @()
    Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.DeviceID -match 'VID_303A' -and $_.Name -match '\((COM\d+)\)' } |
        ForEach-Object {
            if ($_.Name -match '\((COM\d+)\)') { $espPorts += $Matches[1] }
        }
    if ($espPorts.Count -gt 0) {
        $Port = $espPorts[0]
        Write-Host "auto-detected ESP32-S3 USB Serial/JTAG on $Port" -ForegroundColor Green
    }
}

if ($ListOnly -or -not $Port) {
    $null = Show-Ports
    if (-not $Port) {
        Write-Host ""
        Write-Host "Cannot proceed without the USB Serial/JTAG port." -ForegroundColor Red
        Write-Host ""
        Write-Host "Checklist:" -ForegroundColor Yellow
        Write-Host "  1. Micro-USB unplugged" -ForegroundColor Yellow
        Write-Host "  2. GPIO19 and GPIO20 FREE (disconnect I2C: MAX30102 + SHT30)" -ForegroundColor Yellow
        Write-Host "  3. USB-C cable plugged into the PC (data cable, not charge-only)" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "Then re-run:  .\flash_usb.ps1" -ForegroundColor Yellow
        exit 1
    }
}

foreach ($i in $images) {
    if (-not (Test-Path $i.file)) { throw "missing $($i.file) - build first" }
}

Write-Host ""
Write-Host ("=" * 68) -ForegroundColor Cyan
Write-Host " FLASH over USB Serial/JTAG  -  port $Port @ $Baud" -ForegroundColor Cyan
Write-Host ("=" * 68) -ForegroundColor Cyan
Write-Host ""
Write-Host " This path uses --before usb_reset: the USB Serial/JTAG controller" -ForegroundColor Gray
Write-Host " resets the chip into download mode by itself. No button needed." -ForegroundColor Gray
Write-Host ""

$flashArgs = @(
    "--chip", "esp32s3",
    "-p", $Port,
    "-b", "$Baud",
    "--before", "usb_reset",
    "--after", "hard_reset",
    "--connect-attempts", "5",
    "write_flash",
    "--flash_mode", "dio",
    "--flash_freq", "80m",
    "--flash_size", "16MB"
)
foreach ($i in $images) { $flashArgs += @($i.off, $i.file) }

$out = & $py $esp @flashArgs 2>&1
$code = $LASTEXITCODE

$out | Select-String -Pattern "Chip is|MAC:|Wrote|Hash of data verified|Leaving|fatal error|Failed to connect|Invalid head|Wrong boot|USB" |
    Select-Object -Last 12 | ForEach-Object { "  " + $_.Line.Trim() }

Write-Host ""
if ($code -eq 0) {
    Write-Host ("=" * 68) -ForegroundColor Green
    Write-Host " FLASH OK  (over USB Serial/JTAG - no button, no CH343)" -ForegroundColor Green
    Write-Host ("=" * 68) -ForegroundColor Green
    Write-Host ""
    Write-Host " IMPORTANT: the console is still configured for UART0, so to read the" -ForegroundColor Yellow
    Write-Host " boot log you must reconnect the Micro-USB cable and restore GPIO19/20." -ForegroundColor Yellow
    Write-Host ""
    Write-Host " Then capture the log:" -ForegroundColor Cyan
    Write-Host "   .\bringup.ps1 -SkipBuild -SkipFlash -CaptureSeconds 25" -ForegroundColor Cyan
    exit 0
} else {
    Write-Host ("=" * 68) -ForegroundColor Red
    Write-Host " FAILED (USB Serial/JTAG)" -ForegroundColor Red
    Write-Host ("=" * 68) -ForegroundColor Red
    Write-Host ""
    Write-Host " If the port was not detected at all, GPIO19/20 are probably still" -ForegroundColor Yellow
    Write-Host " occupied by the I2C wiring. Disconnect those two wires and retry." -ForegroundColor Yellow
    Write-Host ""
    Write-Host " List ports with:  .\flash_usb.ps1 -ListOnly" -ForegroundColor Yellow
    exit 1
}
