<#
.SYNOPSIS
    Try several DTR/RTS reset timings to find one that puts this board into
    ESP32-S3 download mode.

.DESCRIPTION
    ESP32 auto-download circuit: DTR = EN (RESET), RTS = GPIO0 (BOOT), active low.
    Different USB-UART bridges (CH343 / CP210x / FTDI) and different transistor
    wirings on the board invert the effective polarity or shift the timing window,
    so here we just sweep the common combinations and read the serial port after each.

    Interpretation:
      - silence (or ROM boot text)  -> likely entered download mode
      - old firmware boot log       -> did NOT enter download mode

    NOTE: this file is deliberately ASCII-only. Windows PowerShell 5.1 reads .ps1
    files as GBK on a zh-CN system, which corrupts non-ASCII characters and breaks
    string parsing.
#>
[CmdletBinding()]
param([string]$Port = "COM15")

$ErrorActionPreference = "Continue"

function Try-Sequence {
    param(
        [string]$Name,
        [object[]]$Steps
    )
    Write-Host ""
    Write-Host "===== sequence: $Name =====" -ForegroundColor Cyan

    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, "One"
    try {
        $sp.Open()
    } catch {
        Write-Host "  cannot open port: $($_.Exception.Message)" -ForegroundColor Red
        return
    }

    foreach ($s in $Steps) {
        if ($s.ContainsKey("DTR")) { $sp.DtrEnable = [bool]$s.DTR }
        if ($s.ContainsKey("RTS")) { $sp.RtsEnable = [bool]$s.RTS }
        Start-Sleep -Milliseconds ([int]$s.Ms)
        Write-Host ("  DTR={0,-5} RTS={1,-5} wait {2}ms" -f $sp.DtrEnable, $sp.RtsEnable, $s.Ms)
    }

    $sp.ReadTimeout = 1200
    Start-Sleep -Milliseconds 150
    $data = ""
    try { $data = $sp.ReadExisting() } catch { }
    $sp.Close()

    if ($data.Length -gt 0) {
        $clean = ($data -replace "`e\[[0-9;]*m", "") -replace "[^\x20-\x7E\r\n]", "."
        $first = ($clean -split "`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ } | Select-Object -First 3) -join " | "
        Write-Host "  got $($data.Length) bytes (chip is RUNNING firmware):" -ForegroundColor Yellow
        Write-Host "    $first"
    } else {
        Write-Host "  SILENT - possibly in download mode" -ForegroundColor Green
    }
}

Try-Sequence -Name "A: esptool classic (dtr/rts true -> rts false -> dtr false -> dtr true)" -Steps @(
    @{DTR=$true;  RTS=$true;  Ms=100},
    @{RTS=$false;             Ms=100},
    @{DTR=$false;             Ms=100},
    @{DTR=$true;  RTS=$false; Ms=50},
    @{RTS=$true;              Ms=50}
)

Try-Sequence -Name "B: inverted polarity" -Steps @(
    @{DTR=$false; RTS=$false; Ms=100},
    @{RTS=$true;              Ms=100},
    @{DTR=$true;              Ms=100},
    @{DTR=$false; RTS=$true;  Ms=50},
    @{RTS=$false;             Ms=50}
)

Try-Sequence -Name "C: hold BOOT low, pulse RESET" -Steps @(
    @{DTR=$true;  RTS=$true;  Ms=100},
    @{DTR=$true;  RTS=$false; Ms=150},
    @{DTR=$false; RTS=$false; Ms=150},
    @{DTR=$true;  RTS=$false; Ms=150},
    @{DTR=$true;  RTS=$true;  Ms=100}
)

Try-Sequence -Name "D: toggle DTR only" -Steps @(
    @{DTR=$false; RTS=$true; Ms=100},
    @{DTR=$true;  RTS=$true; Ms=100},
    @{DTR=$false; RTS=$true; Ms=100},
    @{DTR=$true;  RTS=$true; Ms=100}
)

Write-Host ""
Write-Host "If every sequence above printed the OLD FIRMWARE log, the auto-reset circuit" -ForegroundColor Yellow
Write-Host "does not respond to DTR/RTS. Do it by hand instead:" -ForegroundColor Yellow
Write-Host "  hold BOOT -> tap RESET -> release BOOT -> immediately run the flash command." -ForegroundColor Yellow
exit 0
