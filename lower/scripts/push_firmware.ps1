<#
.SYNOPSIS
    Add the GitHub remote to a firmware repo and push it (band and scale).

.DESCRIPTION
    The upper computer (HealthMonitorSystem) already has its origin and was
    pushed separately. The two firmware projects - lower/ (band) and scale/
    (weight scale) - are separate git repositories with no remote at all, so
    nobody can pull them yet.

    This script is the second half: it adds the remote and pushes. You create
    the empty repos on GitHub first (private, no README/.gitignore/license, so
    they start completely empty).

    Branch name is unified as dev-a across all three repos, matching the
    existing HealthMonitorSystem remote.

    Usage:
      .\push_firmware.ps1 -BandUrl https://github.com/GDSH-LGTM/HealthMonitorSystem-lower.git `
                          -ScaleUrl https://github.com/GDSH-LGTM/HealthMonitorSystem-scale.git

      .\push_firmware.ps1 -BandUrl <url>            # only the band
      .\push_firmware.ps1 -Check                    # show current remote state

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads .ps1 files as GBK
    on a zh-CN system, which corrupts non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$BandUrl  = "",
    [string]$ScaleUrl = "",
    [switch]$Check
)

$ErrorActionPreference = "Continue"

# This script lives at <repoRoot>/lower/scripts/, so:
#   $PSScriptRoot      = <repoRoot>/lower/scripts
#   parent             = <repoRoot>/lower        <- the band project
#   parent of that     = <repoRoot>              <- holds HealthMonitorSystem/ and scale/
$bandDir  = Split-Path $PSScriptRoot -Parent
$repoRoot = Split-Path $bandDir -Parent
$upperDir = Join-Path $repoRoot "HealthMonitorSystem"
$scaleDir = Join-Path $repoRoot "scale"

function Show-State($label, $dir) {
    Write-Host "--- $label ---"
    if (-not (Test-Path (Join-Path $dir ".git"))) {
        Write-Host "   not a git repo: $dir" -ForegroundColor Yellow
        return
    }
    $rem = (& git -C $dir remote -v) 2>$null
    if ($rem) { $rem | ForEach-Object { Write-Host "   $_" } } else { Write-Host "   (no remote)" }
    $br = (& git -C $dir branch --show-current) 2>$null
    $n  = (& git -C $dir rev-list --count HEAD) 2>$null
    $dirty = (& git -C $dir status --porcelain) 2>$null
    Write-Host ("   branch={0}  commits={1}  worktree={2}" -f $br, $n, $(if ($dirty) { "DIRTY" } else { "clean" }))
}

if ($Check) {
    Show-State "upper computer" $upperDir
    Show-State "band (lower)"   $bandDir
    Show-State "scale"          $scaleDir
    exit 0
}

if (-not $BandUrl -and -not $ScaleUrl) {
    Write-Host "Nothing to do: pass -BandUrl and/or -ScaleUrl (or use -Check)." -ForegroundColor Yellow
    exit 1
}

function Push-Repo($label, $dir, $url) {
    Write-Host ""
    Write-Host ("=" * 68) -ForegroundColor Cyan
    Write-Host " $label  ->  $url" -ForegroundColor Cyan
    Write-Host ("=" * 68) -ForegroundColor Cyan

    if (-not (Test-Path (Join-Path $dir ".git"))) {
        Write-Host "  not a git repo, skipping: $dir" -ForegroundColor Red
        return $false
    }

    $dirty = (& git -C $dir status --porcelain) 2>$null
    if ($dirty) {
        Write-Host "  WARNING: working tree is dirty, only committed changes go up:" -ForegroundColor Yellow
        $dirty | Select-Object -First 10 | ForEach-Object { Write-Host "    $_" }
    }

    $existing = (& git -C $dir remote get-url origin) 2>$null
    if ($existing) {
        Write-Host "  origin already set to: $existing" -ForegroundColor Gray
        if ($existing -ne $url) {
            Write-Host "  updating to: $url" -ForegroundColor Yellow
            & git -C $dir remote set-url origin $url
        }
    } else {
        & git -C $dir remote add origin $url
        Write-Host "  added origin" -ForegroundColor Green
    }

    $br = (& git -C $dir branch --show-current) 2>$null
    Write-Host "  pushing branch $br ..."

    $env:GIT_TERMINAL_PROMPT = '0'
    $out = & git -C $dir push -u origin $br 2>&1
    $code = $LASTEXITCODE
    $out | Select-Object -Last 10 | ForEach-Object { "    $_" }

    if ($code -eq 0) {
        Write-Host "  PUSHED OK" -ForegroundColor Green
        return $true
    }
    Write-Host "  PUSH FAILED (exit $code)" -ForegroundColor Red
    Write-Host "  If it asked for credentials and failed: GitHub is rejecting the push." -ForegroundColor Yellow
    Write-Host "  Options: sign in with a Personal Access Token, or configure a" -ForegroundColor Yellow
    Write-Host "  credential manager, or add an SSH key." -ForegroundColor Yellow
    return $false
}

$okBand  = $true
$okScale = $true
if ($BandUrl)  { $okBand  = Push-Repo "band (lower)" $bandDir  $BandUrl }
if ($ScaleUrl) { $okScale = Push-Repo "scale"        $scaleDir $ScaleUrl }

Write-Host ""
Write-Host "=== final state ===" -ForegroundColor Cyan
Show-State "band (lower)" $bandDir
Show-State "scale"        $scaleDir

Write-Host ""
if ($okBand -and $okScale) {
    Write-Host "Done. Give the collaborator the repository URLs." -ForegroundColor Green
    Write-Host ""
    Write-Host "IMPORTANT - the firmware needs two extra steps after cloning:" -ForegroundColor Yellow
    Write-Host "  1. third_party/ (LVGL + esp32-camera) is NOT in git - it is ~1400 files." -ForegroundColor Yellow
    Write-Host "     Run .\scripts\fetch_deps.ps1 to regenerate it. That script copies from" -ForegroundColor Yellow
    Write-Host "     lower_old_archive/ which is NOT in the repo either, so on a fresh" -ForegroundColor Yellow
    Write-Host "     machine use the fallback the script prints:" -ForegroundColor Yellow
    Write-Host "       idf.py add-dependency lvgl/lvgl^8.4.0" -ForegroundColor Yellow
    Write-Host "       idf.py add-dependency espressif/esp32-camera^2.1.8" -ForegroundColor Yellow
    Write-Host "  2. scale/ references ../lower/components/fw_core/fw_proto.c, so both" -ForegroundColor Yellow
    Write-Host "     clones must sit side by side." -ForegroundColor Yellow
} else {
    Write-Host "Some pushes failed - see the output above." -ForegroundColor Red
}
