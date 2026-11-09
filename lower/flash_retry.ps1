<#
.SYNOPSIS
    Flash the band firmware with settings tuned for a board whose boot window
    keeps slipping: lower baud, repeated attempts, clear per-attempt feedback.

.DESCRIPTION
    Why this exists, from the actual failure log:
        attempt 1: No serial data received
        attempt 2: Invalid head of packet (0x31): Possible serial noise or corruption
        attempt 3: No serial data received

    Attempt 2 is the informative one. "Invalid head of packet" means the chip WAS
    in download mode and DID answer - the sync just came back corrupted. "No
    serial data received" means the chip was running the app instead. So the
    button combo works; two things are unreliable:

      1. BAUD. 460800 over this CH343 link corrupts the sync. 115200 is far more
         forgiving and is what this script defaults to.
      2. TIMING. esptool prints warnings and probes for the chip before it tries
         to sync. By then the ROM's window may have closed. Using
         --before no_reset skips the reset attempt but esptool still takes a
         moment to start; the button must be pressed as the countdown hits zero,
         and re-tapped on every attempt.

    Usage:
      .\flash_retry.ps1                       # 115200, 5 attempts
      .\flash_retry.ps1 -Attempts 8 -WaitSeconds 12
      .\flash_retry.ps1 -Baud 230400

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 files as GBK
    on a zh-CN system, which corrupts non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$Baud = 0,                 # 0 = sweep the list below
    [int]$Attempts = 6,
    [int]$WaitSeconds = 12
)

# Baud sweep order. The failure log showed "Invalid head of packet (0x31)",
# which means the chip answered but the link corrupted - that is a baud problem,
# not a button problem. Try the most practical rate first, then fall back.
$baudList = @(115200, 74880, 230400, 460800)
if ($Baud -gt 0) { $baudList = @($Baud) }

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

Write-Host ""
Write-Host ("=" * 68) -ForegroundColor Cyan
Write-Host " FLASH RETRY - $Attempts attempts, baud sweep: $($baudList -join ', ')" -ForegroundColor Cyan
Write-Host ("=" * 68) -ForegroundColor Cyan
Write-Host ""
Write-Host " This board cannot be reset in software. Do this ON EVERY ATTEMPT," -ForegroundColor Yellow
Write-Host " the moment the countdown reaches zero:" -ForegroundColor Yellow
Write-Host ""
Write-Host "   1. HOLD BOOT" -ForegroundColor White
Write-Host "   2. TAP RESET" -ForegroundColor White
Write-Host "   3. RELEASE BOOT" -ForegroundColor White
Write-Host ""
Write-Host " You can also just keep tapping the combo repeatedly during the whole" -ForegroundColor Gray
Write-Host " countdown - extra resets cannot hurt." -ForegroundColor Gray
Write-Host ""

$done = $false
for ($n = 1; $n -le $Attempts; $n++) {
    $useBaud = $baudList[($n - 1) % $baudList.Count]

    Write-Host ""
    Write-Host ("--- attempt {0} / {1}   baud {2} ---" -f $n, $Attempts, $useBaud) -ForegroundColor Cyan

    for ($t = $WaitSeconds; $t -gt 0; $t--) {
        Write-Host ("`r  hold BOOT -> tap RESET -> release BOOT   ({0,2}s)  " -f $t) -NoNewline
        Start-Sleep -Seconds 1
    }
    Write-Host "`r  flashing now ...                                    "

    $flashArgs = @(
        "--chip", "esp32s3",
        "-p", $Port,
        "-b", "$useBaud",
        "--before", "no_reset",
        "--after", "hard_reset",
        "--connect-attempts", "10",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "16MB"
    )
    foreach ($i in $images) { $flashArgs += @($i.off, $i.file) }

    $out = & $py $esp @flashArgs 2>&1
    $code = $LASTEXITCODE

    $out | Select-String -Pattern "Chip is|MAC:|Wrote|Hash of data verified|Leaving|fatal error|Failed to connect|Invalid head" |
        Select-Object -Last 8 | ForEach-Object { "    " + $_.Line.Trim() }

    if ($code -eq 0) { $done = $true; break }

    # Interpret the failure so the next attempt can be adjusted
    if ($out -match "Invalid head of packet") {
        Write-Host "    -> chip WAS in download mode but the link corrupted at $useBaud." -ForegroundColor Yellow
        Write-Host "       the next attempt drops to a lower baud automatically." -ForegroundColor Yellow
    } elseif ($out -match "No serial data received") {
        Write-Host "    -> chip was still running the app: the button combo was missed." -ForegroundColor Yellow
        Write-Host "       press it as the countdown hits zero, not before." -ForegroundColor Yellow
    } elseif ($out -match "Wrong boot mode") {
        Write-Host "    -> chip booted normally: BOOT was not held through RESET." -ForegroundColor Yellow
    }
}

Write-Host ""
if ($done) {
    Write-Host ("=" * 68) -ForegroundColor Green
    Write-Host " FLASH OK" -ForegroundColor Green
    Write-Host ("=" * 68) -ForegroundColor Green
    Write-Host ""
    Write-Host " Now capture the boot log:" -ForegroundColor Cyan
    Write-Host "   .\bringup.ps1 -SkipBuild -SkipFlash -CaptureSeconds 25" -ForegroundColor Cyan
    exit 0
} else {
    Write-Host ("=" * 68) -ForegroundColor Red
    Write-Host " STILL FAILING after $Attempts attempts" -ForegroundColor Red
    Write-Host ("=" * 68) -ForegroundColor Red
    Write-Host ""
    Write-Host " Next steps, in order:" -ForegroundColor Yellow
    Write-Host "   1. confirm BOOT reaches GPIO0:  .\probe_reset.ps1" -ForegroundColor Yellow
    Write-Host "      reading boot:0x0 = BOOT works; boot:0xc = it does not." -ForegroundColor Yellow
    Write-Host "   2. if this board has no BOOT button at all, short GPIO0 to GND" -ForegroundColor Yellow
    Write-Host "      instead of step 1, then tap RESET." -ForegroundColor Yellow
    Write-Host "   3. tell me the button silkscreen on your board - if there is no" -ForegroundColor Yellow
    Write-Host "      BOOT/IO0 button, that changes what we do next." -ForegroundColor Yellow
    exit 1
}
