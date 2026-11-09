<#
.SYNOPSIS
    Interactive flash: you put the board into download mode by hand, the script
    waits, then flashes immediately.

.DESCRIPTION
    This board's USB-UART auto-reset circuit does not respond to DTR/RTS
    (every automated sequence still reports "boot:0xc (SPI_FAST_FLASH_BOOT)",
    i.e. GPIO0 high = normal boot). So we do it the manual way:

        hold BOOT  ->  tap RESET  ->  release BOOT

    After that the chip sits in download mode until something talks to it.

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 as GBK on a
    zh-CN system and would corrupt non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$Baud = 460800,
    [int]$WaitSeconds = 40
)

$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot

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
    if (-not (Test-Path $i.file)) {
        throw "missing image $($i.file) - run 'build' first"
    }
}

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host " Put the board into DOWNLOAD MODE now:" -ForegroundColor Yellow
Write-Host ""
Write-Host "   1. HOLD the BOOT button (do not release)" -ForegroundColor White
Write-Host "   2. TAP the RESET (EN) button once" -ForegroundColor White
Write-Host "   3. RELEASE the BOOT button" -ForegroundColor White
Write-Host ""
Write-Host " The board stays in download mode, so there is no rush." -ForegroundColor Gray
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

for ($t = $WaitSeconds; $t -gt 0; $t--) {
    Write-Host ("`r  flashing in {0,2} s ...  " -f $t) -NoNewline
    Start-Sleep -Seconds 1
}
Write-Host "`r  go!                          "

$flashArgs = @(
    "--chip", "esp32s3",
    "-p", $Port,
    "-b", "$Baud",
    "--before", "no_reset",
    "--after", "hard_reset",
    "write_flash",
    "--flash_mode", "dio",
    "--flash_freq", "80m",
    "--flash_size", "16MB"
)
foreach ($i in $images) { $flashArgs += @($i.off, $i.file) }

# Give the user a chance to re-do the button combo if the first try missed.
$attempt = 0
$maxAttempts = 3
while ($attempt -lt $maxAttempts) {
    $attempt++
    Write-Host ""
    Write-Host "--- attempt $attempt / $maxAttempts ---" -ForegroundColor Cyan
    if ($attempt -gt 1) {
        Write-Host "Previous attempt failed. Re-do the button combo:" -ForegroundColor Yellow
        Write-Host "  HOLD BOOT -> tap RESET -> release BOOT" -ForegroundColor Yellow
        for ($t = 20; $t -gt 0; $t--) {
            Write-Host ("`r  retrying in {0,2} s ...  " -f $t) -NoNewline
            Start-Sleep -Seconds 1
        }
        Write-Host "`r  go!                          "
    }

    & $py $esp @flashArgs
    $code = $LASTEXITCODE
    if ($code -eq 0) { break }
}

Write-Host ""
if ($code -eq 0) {
    Write-Host "FLASH OK" -ForegroundColor Green
} else {
    Write-Host "FLASH FAILED (exit $code)" -ForegroundColor Red
    Write-Host "If it says 'Wrong boot mode', the board booted normally instead of" -ForegroundColor Yellow
    Write-Host "entering download mode - the BOOT button was not held through the RESET." -ForegroundColor Yellow
    Write-Host "If it says 'No serial data received', try -Baud 115200." -ForegroundColor Yellow
}
exit $code
