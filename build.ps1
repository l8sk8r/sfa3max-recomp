<#
.SYNOPSIS
    One-shot build of sfa3r.exe: update main, regenerate the translated C, configure and build.

.DESCRIPTION
    Runs, in order:
        git checkout main
        git pull
        python tools/prepare.py
        cmake -B out -G "Visual Studio 18 2026" -A x64
        cmake --build out --config Release
    and stops at the first step that fails.

.EXAMPLE
    .\build.ps1                  # the standard build from main (ISO in imports\psp\)
    .\build.ps1 -ForceWideScreen:$false   # without the SFA3MAX 16:9 patch (no WIDE FORCED)
    .\build.ps1 -Here            # build the branch you are on (no checkout, still pulls)
    .\build.ps1 -NoPull -Run     # skip the pull, launch the game afterwards
#>
param(
    [string]$Iso = "",          # your ISO, if not imports\psp\Street Fighter Alpha 3 Max.iso (or SFA3R_ISO)
    [string]$Branch = "main",   # branch to check out before building
    [switch]$Here,              # stay on the current branch instead of checking out $Branch
    [switch]$NoPull,            # skip git pull
    [switch]$ForceWideScreen = $true,   # include the 16:9 patch (SCREEN SIZE > WIDE FORCED); $false leaves it out
    [switch]$SkipPrepare,       # reuse the existing work/ and build/generated/
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [switch]$Run                # start the game after a successful build
)

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

function Step([string]$Title, [scriptblock]$Action) {
    Write-Host ""
    Write-Host "==> $Title" -ForegroundColor Cyan
    & $Action
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $Title (exit code $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

$timer = [System.Diagnostics.Stopwatch]::StartNew()

if (-not $Here) {
    # Don't let a checkout carry uncommitted work onto another branch.
    if (git status --porcelain --untracked-files=no) {
        Write-Host "You have uncommitted changes; commit or stash them, or use -Here to build this branch." -ForegroundColor Yellow
        exit 1
    }
    Step "git checkout $Branch" { git checkout $Branch }
}

if (-not $NoPull) {
    Step "git pull" { git pull }
}

if (-not $SkipPrepare) {
    $prepareArgs = @("tools/prepare.py")
    if ($Iso) { $prepareArgs += $Iso }
    if (-not $ForceWideScreen) { $prepareArgs += "--no-widescreen" }
    Step "python $($prepareArgs -join ' ')" { python @prepareArgs }
}

Step "cmake configure" { cmake -B out -G "Visual Studio 18 2026" -A x64 }
Step "cmake build ($Config)" { cmake --build out --config $Config }

$timer.Stop()
Write-Host ""
Write-Host ("Build succeeded on '{0}' in {1:mm\:ss}: out\{2}\sfa3r.exe" -f (git branch --show-current), $timer.Elapsed, $Config) -ForegroundColor Green

if ($Run) {
    Step "run sfa3r" { & ".\out\$Config\sfa3r.exe" --work work }
}
