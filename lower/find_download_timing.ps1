<#
.SYNOPSIS
    Sweep DTR/RTS reset timings and read the ROM boot log to find which timing
    actually pulls GPIO0 low (= download mode).

.DESCRIPTION
    The ESP32-S3 ROM prints a boot line within ~100 ms of a reset:

        ESP-ROM:esp32s3-20210327 | Build:Mar 27 2021 | rst:0x1 (POWERON),boot:0xc (SPI_FAST_FLASH_BOOT)

    boot:0xc  -> GPIO0 high -> normal SPI flash boot (NOT what we want)
    boot:0x0  -> GPIO0 low  -> download mode (what we want)

    So instead of guessing, reset with a given timing, read the line, and parse the
    hex value. The script reports the parsed boot mode for each timing.

    DTR = EN (RESET), RTS = GPIO0 (BOOT), both active low, on the usual wiring.

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 as GBK on a
    zh-CN system and would corrupt non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$ReadMs = 900
)

$ErrorActionPreference = "Continue"

# Read the ROM boot line after applying a reset timing.
function Get-BootMode {
    param([object[]]$Steps)

    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, "One"
    try { $sp.Open() } catch { return @{ err = $_.Exception.Message } }

    $sp.DiscardInBuffer()
    foreach ($s in $Steps) {
        if ($s.ContainsKey("DTR")) { $sp.DtrEnable = [bool]$s.DTR }
        if ($s.ContainsKey("RTS")) { $sp.RtsEnable = [bool]$s.RTS }
        Start-Sleep -Milliseconds ([int]$s.Ms)
    }

    $sp.ReadTimeout = $ReadMs
    Start-Sleep -Milliseconds 120
    $data = ""
    try { $data = $sp.ReadExisting() } catch { }
    $sp.Close()

    $clean = ($data -replace "`e\[[0-9;]*m", "") -replace "[^\x20-\x7E\r\n]", "."
    $boot = $null
    $rst  = $null
    if ($clean -match "boot:(0x[0-9a-fA-F]+)") { $boot = $Matches[1] }
    if ($clean -match "rst:(0x[0-9a-fA-F]+)") { $rst  = $Matches[1] }

    return @{ boot = $boot; rst = $rst; len = $data.Length; text = $clean }
}

# Timing families to sweep.
#   holdMs : how long BOOT is held low before RESET is pulsed
#   pulseMs: how long RESET is held low
$families = @(
    @{ name = "A: rts->low, then dtr->low, then dtr->high, then rts->high";
       seq = { param($h,$p) @(
            @{DTR=$true;  RTS=$true;  Ms=$h},
            @{DTR=$true;  RTS=$false; Ms=$h},
            @{DTR=$false; RTS=$false; Ms=$p},
            @{DTR=$true;  RTS=$false; Ms=$h},
            @{DTR=$true;  RTS=$true;  Ms=$h} ) } },
    @{ name = "B: dtr->low first, then rts->low";
       seq = { param($h,$p) @(
            @{DTR=$true;  RTS=$true;  Ms=$h},
            @{DTR=$false; RTS=$true;  Ms=$h},
            @{DTR=$false; RTS=$false; Ms=$p},
            @{DTR=$false; RTS=$true;  Ms=$h},
            @{DTR=$true;  RTS=$true;  Ms=$h} ) } },
    @{ name = "C: both low together, release rts first";
       seq = { param($h,$p) @(
            @{DTR=$true;  RTS=$true;  Ms=$h},
            @{DTR=$false; RTS=$false; Ms=$p},
            @{DTR=$false; RTS=$true;  Ms=$h},
            @{DTR=$true;  RTS=$true;  Ms=$h} ) } },
    @{ name = "D: both low together, release dtr first";
       seq = { param($h,$p) @(
            @{DTR=$true;  RTS=$true;  Ms=$h},
            @{DTR=$false; RTS=$false; Ms=$p},
            @{DTR=$true;  RTS=$false; Ms=$h},
            @{DTR=$true;  RTS=$true;  Ms=$h} ) } },
    @{ name = "E: inverted (dtr low first)";
       seq = { param($h,$p) @(
            @{DTR=$false; RTS=$false; Ms=$h},
            @{DTR=$false; RTS=$true;  Ms=$h},
            @{DTR=$true;  RTS=$true;  Ms=$p},
            @{DTR=$true;  RTS=$false; Ms=$h},
            @{DTR=$false; RTS=$false; Ms=$h} ) } }
)

$holds  = @(50, 200, 500)
$pulses = @(50, 200, 500)

Write-Host "Sweeping reset timings on $Port ..." -ForegroundColor Cyan
Write-Host ""
Write-Host ("{0,-58} {1,-8} {2,-8} {3}" -f "sequence", "boot", "rst", "note")
Write-Host ("-" * 92)

$hits = @()

foreach ($f in $families) {
    foreach ($h in $holds) {
        foreach ($p in $pulses) {
            $steps = & $f.seq $h $p
            $r = Get-BootMode -Steps $steps
            $note = ""
            if ($r.err) {
                $note = "open failed: $($r.err)"
                $boot = "-"
            } elseif ($r.boot) {
                $boot = $r.boot
                if ($r.boot -match "0x0$" -or $r.boot -eq "0x0") {
                    $note = "*** DOWNLOAD MODE ***"
                    $hits += ,@{ family = $f.name; hold = $h; pulse = $p }
                } elseif ($r.boot -match "0x[0-9a-fA-F]$") {
                    $note = "normal boot (GPIO0 high)"
                }
            } else {
                $boot = "silent"
                $note = "no ROM line ($($r.len) bytes)"
            }
            Write-Host ("{0,-58} {1,-8} {2,-8} {3}" -f ("$($f.name) [hold=$h pulse=$p]"), $boot, $r.rst, $note)
        }
    }
}

Write-Host ""
if ($hits.Count -gt 0) {
    Write-Host "Timings that produced DOWNLOAD MODE:" -ForegroundColor Green
    foreach ($h in $hits) { Write-Host ("  {0}  hold={1} pulse={2}" -f $h.family, $h.hold, $h.pulse) -ForegroundColor Green }
} else {
    Write-Host "No timing produced download mode." -ForegroundColor Yellow
    Write-Host "This board's DTR/RTS are probably not wired to EN/GPIO0 at all." -ForegroundColor Yellow
    Write-Host "Use the BOOT+RESET buttons by hand, or use esptool's built-in reset:" -ForegroundColor Yellow
    Write-Host "  esptool.py --chip esp32s3 -p COM15 -b 115200 --before default_reset write_flash ..." -ForegroundColor Yellow
}
exit 0
