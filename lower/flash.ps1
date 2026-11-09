<#
.SYNOPSIS
    ESP-IDF environment setup + idf.py wrapper for the band firmware.

.DESCRIPTION
    ESP-IDF on this machine lives at a non-default path (D:\ESP_IDF\Espressif) and
    export.ps1 was not used, so the environment has to be laid out by hand:
    IDF_PATH / IDF_TOOLS_PATH / toolchain PATH / ESP_ROM_ELF_DIR.
    This script does that, then forwards the command to idf.py from this folder.

.EXAMPLE
    .\flash.ps1 build
    .\flash.ps1 flash
    .\flash.ps1 monitor
    .\flash.ps1 flash monitor
    .\flash.ps1 -Port COM15 set-target esp32s3

.NOTES
    ASCII-only on purpose. Windows PowerShell 5.1 reads .ps1 files as GBK on a
    zh-CN system, which turns non-ASCII characters into mojibake - and when they
    sit inside string literals the output is garbled, when they sit in comments
    the parser can fail outright. This bit us repeatedly, so all scripts in this
    project are ASCII-only from here on.
#>
[CmdletBinding()]
param(
    [string]$Port = "COM15",
    [int]$Baud = 460800,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs = @("build")
)

$ErrorActionPreference = "Stop"

$IdfRoot        = "D:\ESP_IDF\Espressif"
$IdfFramework   = "$IdfRoot\frameworks\esp-idf-v5.5.5"
$IdfPythonEnv   = "$IdfRoot\python_env\idf5.5_py3.11_env"
$IdfPython      = "$IdfPythonEnv\Scripts\python.exe"

if (-not (Test-Path $IdfPython)) {
    throw "IDF python not found: $IdfPython - check the ESP-IDF install path"
}

$env:IDF_PATH            = $IdfFramework
$env:IDF_TOOLS_PATH      = $IdfRoot
$env:IDF_PYTHON_ENV_PATH = $IdfPythonEnv
$env:ESP_ROM_ELF_DIR     = "$IdfRoot\tools\esp-rom-elfs\20241011"
$env:PYTHONPATH          = ""
$env:PYTHONHOME          = ""
$env:PYTHONNOUSERSITE    = "True"

$toolDirs = @(
    "$IdfRoot\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin",
    "$IdfRoot\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin",
    "$IdfRoot\tools\ninja\1.12.1",
    "$IdfRoot\tools\cmake\3.30.2\bin",
    "$IdfRoot\tools\idf-exe\1.0.3",
    "$IdfRoot\tools\ccache\4.12.1",
    "$IdfRoot\tools\idf-git\2.44.0\cmd",
    "$IdfPythonEnv\Scripts"
)
$missing = $toolDirs | Where-Object { -not (Test-Path $_) }
if ($missing) {
    Write-Warning ("tool directories missing, the build may fail:`n  " + ($missing -join "`n  "))
}
$env:PATH = ($toolDirs -join ";") + ";" + $env:PATH

Set-Location $PSScriptRoot

# Insert -p / -b ahead of idf.py's subcommand (idf.py requires that order).
$forward = @()
foreach ($a in $IdfArgs) {
    if ($a -in @("build", "flash", "monitor", "clean", "fullclean",
                 "menuconfig", "erase-flash", "size", "size-components",
                 "set-target", "reconfigure")) {
        $forward += @("-p", $Port, "-b", "$Baud")
    }
    $forward += $a
}

Write-Host "IDF : $IdfFramework" -ForegroundColor DarkGray
Write-Host "Port: $Port @ $Baud" -ForegroundColor DarkGray
Write-Host ("Run : idf.py " + ($forward -join " ")) -ForegroundColor DarkGray

& $IdfPython "$IdfFramework\tools\idf.py" @forward
exit $LASTEXITCODE
