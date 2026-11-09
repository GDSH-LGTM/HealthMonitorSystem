<#
.SYNOPSIS
    Build / flash / monitor the weight-scale firmware.

.DESCRIPTION
    Sets up the same ESP-IDF environment as the band project's flash.ps1 (same
    install path, same toolchain), then runs idf.py here in the scale project.

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 as GBK on a
    zh-CN system and would corrupt non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$Baud = 460800,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs = @("build")
)

$ErrorActionPreference = "Stop"

$scaleRoot = $PSScriptRoot
$repoRoot  = Split-Path $scaleRoot -Parent

if (-not (Test-Path (Join-Path $repoRoot "lower"))) {
    throw "expected the band project at $repoRoot\lower - scale\ must sit next to it"
}

$idfRoot      = "D:\ESP_IDF\Espressif"
$idfFramework = "$idfRoot\frameworks\esp-idf-v5.5.5"
$idfPythonEnv = "$idfRoot\python_env\idf5.5_py3.11_env"
$idfPython    = "$idfPythonEnv\Scripts\python.exe"

if (-not (Test-Path $idfPython)) { throw "idf python not found: $idfPython" }

$env:IDF_PATH            = $idfFramework
$env:IDF_TOOLS_PATH      = $idfRoot
$env:IDF_PYTHON_ENV_PATH = $idfPythonEnv
$env:ESP_ROM_ELF_DIR     = "$idfRoot\tools\esp-rom-elfs\20241011"
$env:PYTHONPATH          = ""
$env:PYTHONHOME          = ""
$env:PYTHONNOUSERSITE    = "True"

$toolDirs = @(
    "$idfRoot\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin",
    "$idfRoot\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin",
    "$idfRoot\tools\ninja\1.12.1",
    "$idfRoot\tools\cmake\3.30.2\bin",
    "$idfRoot\tools\idf-exe\1.0.3",
    "$idfRoot\tools\ccache\4.12.1",
    "$idfRoot\tools\idf-git\2.44.0\cmd",
    "$idfPythonEnv\Scripts"
)
$env:PATH = ($toolDirs -join ";") + ";" + $env:PATH

Set-Location $scaleRoot

$forward = @()
foreach ($a in $IdfArgs) {
    if ($a -in @("build", "flash", "monitor", "clean", "fullclean",
                 "menuconfig", "erase-flash", "size", "set-target", "reconfigure")) {
        $forward += @("-p", $Port, "-b", "$Baud")
    }
    $forward += $a
}

Write-Host "scale project : $scaleRoot" -ForegroundColor DarkGray
Write-Host "port          : $Port @ $Baud" -ForegroundColor DarkGray
Write-Host ("idf.py " + ($forward -join " ")) -ForegroundColor DarkGray

& $idfPython "$idfFramework\tools\idf.py" @forward
exit $LASTEXITCODE
