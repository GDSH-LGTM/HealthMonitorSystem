<#
.SYNOPSIS
    Interactive probe: watch the serial port while you press the board buttons.

.DESCRIPTION
    This board has no working auto-reset circuit (all 45 DTR/RTS timing
    combinations still boot with GPIO0 high = boot:0xc), so flashing depends on
    the physical BOOT and RESET buttons. This script tells us whether those
    buttons actually reach the chip.

    What to do while it runs (it prompts you):
      Step 1: press RESET only.
              -> a fresh ROM line should appear. That proves the button works.
      Step 2: HOLD BOOT, tap RESET, release BOOT.
              -> ROM line should show boot:0x0 (download mode) or be silent.

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 as GBK on a
    zh-CN system and would corrupt non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$Seconds = 25
)

$ErrorActionPreference = "Continue"

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, "One"
try {
    $sp.Open()
} catch {
    Write-Host "cannot open $Port : $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
$sp.ReadTimeout = 200

Write-Host ""
Write-Host "Watching $Port for $Seconds s. Do this:" -ForegroundColor Cyan
Write-Host ""
Write-Host "  [1] Press RESET (EN) only ................" -ForegroundColor Yellow
Write-Host "      ... wait for the script to say it saw a reset ..." -ForegroundColor Gray
Write-Host "  [2] HOLD BOOT, tap RESET, release BOOT ..." -ForegroundColor Yellow
Write-Host "      ... hold BOOT for the whole tap ..." -ForegroundColor Gray
Write-Host ""
Write-Host "Tapping RESET the moment the script says 'NOW' works best." -ForegroundColor Gray
Write-Host ""

$buf = ""
$seenBoot = 0
$seenDownload = 0
$deadline = (Get-Date).AddSeconds($Seconds)
$nextPrompt = (Get-Date)

while ((Get-Date) -lt $deadline) {
    try {
        $chunk = $sp.ReadExisting()
    } catch {
        $chunk = ""
    }
    if ($chunk.Length -gt 0) {
        $buf += $chunk
    }

    # Parse ROM boot lines as they arrive
    while ($buf -match "boot:(0x[0-9a-fA-F]+)") {
        $boot = $Matches[1]
        $seenBoot++
        $color = "Yellow"
        $verdict = "normal boot (GPIO0 high)"
        if ($boot -match "^0x0+$") {
            $seenDownload++
            $color = "Green"
            $verdict = "*** DOWNLOAD MODE - GPIO0 was low ***"
        }
        Write-Host ("  [{0}] ROM boot line: boot={1}  {2}" -f (Get-Date -Format "HH:mm:ss"), $boot, $verdict) -ForegroundColor $color

        # Trim the buffer past this match so we do not re-report it
        $idx = $buf.IndexOf("boot:$boot")
        $buf = $buf.Substring($idx + 8)
    }

    # Print any other non-empty text lines (firmware logs)
    $lines = $buf -split "`n"
    if ($lines.Count -gt 1) {
        for ($i = 0; $i -lt $lines.Count - 1; $i++) {
            $l = ($lines[$i] -replace "`e\[[0-9;]*m", "") -replace "[^\x20-\x7E]", ""
            $l = $l.Trim()
            if ($l.Length -gt 3) { Write-Host "  fw: $l" -ForegroundColor DarkGray }
        }
        $buf = $lines[-1]
    }

    if ((Get-Date) -ge $nextPrompt) {
        $left = [int]($deadline - (Get-Date)).TotalSeconds
        Write-Host ("  --- {0}s left --- " -f $left) -ForegroundColor DarkCyan
        $nextPrompt = (Get-Date).AddSeconds(5)
    }

    Start-Sleep -Milliseconds 50
}

$sp.Close()

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ("ROM boot lines seen : {0}" -f $seenBoot)
Write-Host ("Download mode seen  : {0}" -f $seenDownload) -ForegroundColor $(if ($seenDownload -gt 0) { "Green" } else { "Red" })
Write-Host ""
if ($seenBoot -eq 0) {
    Write-Host "No ROM boot line at all." -ForegroundColor Yellow
    Write-Host "-> Either the buttons do not reach the chip, or the board is not powered," -ForegroundColor Yellow
    Write-Host "   or the console UART is not on this port. Check the USB cable and power LED." -ForegroundColor Yellow
} elseif ($seenDownload -eq 0) {
    Write-Host "Reset works, but BOOT(GPIO0) never went low." -ForegroundColor Yellow
    Write-Host "-> Hold BOOT firmly BEFORE tapping RESET, and only release it after RESET is out." -ForegroundColor Yellow
    Write-Host "-> Also check that the BOOT button is not stuck or mislabeled on this expansion board." -ForegroundColor Yellow
} else {
    Write-Host "Download mode reached - run flash_manual.ps1 next." -ForegroundColor Green
}
exit 0
