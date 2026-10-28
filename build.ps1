<#
.SYNOPSIS
    One-shot build of sfa3r.exe: extract and translate your disc, then compile.

.DESCRIPTION
    Nothing needs installing: the first time, the script downloads its own build tools into .tools\
    (about 300 MB: the LLVM-MinGW compiler, CMake, Ninja and, if Python 3.9+ is not installed,
    Python), each checked against a known SHA-256 where one is pinned. Then it runs:
        python tools/prepare.py      (extract the ISO, translate the game to C)
        cmake configure + build      (out\Release\sfa3r.exe)
    and stops at the first step that fails.

    -VisualStudio builds with Visual Studio 2026 instead (for developers who have it).
    In a git clone it also checks out and pulls main first, unless -Here / -NoPull.

.EXAMPLE
    .\build.ps1 -Run             # build and play (ISO in imports\psp\)
    .\build.ps1 -ForceWideScreen:$false   # without the SFA3MAX 16:9 patch (no WIDE FORCED)
    .\build.ps1 -VisualStudio    # use Visual Studio 2026 instead of the downloaded tools
    .\build.ps1 -Here            # git clone: build the branch you are on (no checkout, still pulls)
#>
param(
    [string]$Iso = "",          # your ISO, if not imports\psp\Street Fighter Alpha 3 Max.iso (or SFA3R_ISO)
    [string]$Branch = "main",   # git clone: branch to check out before building
    [switch]$Here,              # git clone: stay on the current branch instead of checking out $Branch
    [switch]$NoPull,            # git clone: skip git pull
    [switch]$ForceWideScreen = $true,   # include the 16:9 patch (SCREEN SIZE > WIDE FORCED); $false leaves it out
    [switch]$SkipPrepare,       # reuse the existing work/ and build/generated/
    [switch]$VisualStudio,      # build with Visual Studio 2026 rather than the downloaded tools
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [switch]$Run                # start the game after a successful build
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"     # Invoke-WebRequest is many times slower with its progress bar
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

# ---------------------------------------------------------------- build tools (downloaded once into .tools\)
$Tools = Join-Path $PSScriptRoot ".tools"
$Downloads = @{
    llvm   = @{ Url = "https://github.com/mstorsjo/llvm-mingw/releases/download/20251104/llvm-mingw-20251104-ucrt-x86_64.zip"
                Sha = "171b298f46fa4b5653dc7b01cc5e709caca645991be8e1b0724466c4a282a1ac"; Dir = "llvm-mingw" }
    cmake  = @{ Url = "https://github.com/Kitware/CMake/releases/download/v4.1.2/cmake-4.1.2-windows-x86_64.zip"
                Sha = "82730741149681597510a4bc7095da1460b472dac24eaf9c23ad7440f47753ab"; Dir = "cmake" }
    ninja  = @{ Url = "https://github.com/ninja-build/ninja/releases/download/v1.13.1/ninja-win.zip"
                Sha = "26a40fa8595694dec2fad4911e62d29e10525d2133c9a4230b66397774ae25bf"; Dir = "ninja" }
    python = @{ Url = "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip"
                Sha = ""; Dir = "python" }
}

function Get-Tool([string]$Name) {
    $d = $Downloads[$Name]
    $dest = Join-Path $Tools $d.Dir
    if (Test-Path (Join-Path $dest ".ready")) { return $dest }
    New-Item -ItemType Directory -Force -Path $Tools | Out-Null
    $zip = Join-Path $Tools ($d.Dir + ".zip")
    Write-Host "  downloading $Name ($($d.Url))"
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {     # (in Windows 10+; faster than Invoke-WebRequest)
        curl.exe -fsSL --retry 3 -o $zip $d.Url
        if ($LASTEXITCODE -ne 0) { throw "could not download $Name" }
    } else {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $d.Url -OutFile $zip -UseBasicParsing
    }
    if ($d.Sha -and ((Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower() -ne $d.Sha)) {
        Remove-Item $zip -Force
        throw "$Name download does not match its SHA-256; try again, or report it"
    }
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    $tmp = "$dest.tmp"
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    Write-Host "  unpacking $Name"
    tar.exe -xf $zip -C $tmp                  # Windows 10+ tar reads zip, much faster than Expand-Archive
    if ($LASTEXITCODE -ne 0) { throw "could not unpack $zip" }
    $inner = @(Get-ChildItem $tmp)
    if ($inner.Count -eq 1 -and $inner[0].PSIsContainer) { Move-Item $inner[0].FullName $dest; Remove-Item $tmp -Recurse -Force }
    else { Move-Item $tmp $dest }
    Remove-Item $zip -Force
    if ($Name -eq "python") { Remove-Item (Join-Path $dest "*._pth") -Force }   # a normal sys.path for tools\
    New-Item -ItemType File -Path (Join-Path $dest ".ready") | Out-Null
    return $dest
}

function Find-Python {
    foreach ($exe in @("python", "py")) {
        $cmd = Get-Command $exe -ErrorAction SilentlyContinue
        if (-not $cmd -or $cmd.Source -like "*WindowsApps*") { continue }   # (the Store's stand-in, not Python)
        $v = & $exe -c "import sys; print(sys.version_info >= (3, 9))" 2>$null
        if ($v -eq "True") { return $cmd.Source }
    }
    return (Join-Path (Get-Tool "python") "python.exe")
}

$timer = [System.Diagnostics.Stopwatch]::StartNew()

# ---------------------------------------------------------------- git (a clone only)
$isClone = Test-Path (Join-Path $PSScriptRoot ".git")
if (-not $isClone) { $Here = $true; $NoPull = $true }
if (-not $Here) {
    # Don't let a checkout carry uncommitted work onto another branch.
    if (git status --porcelain --untracked-files=no) {
        Write-Host "You have uncommitted changes; commit or stash them, or use -Here to build this branch." -ForegroundColor Yellow
        exit 1
    }
    Step "git checkout $Branch" { git checkout $Branch }
}
if (-not $NoPull) { Step "git pull" { git pull } }

# ---------------------------------------------------------------- tools
if ($VisualStudio) {
    $python = Find-Python
} else {
    Step "build tools (.tools\)" {
        $script:llvm = Get-Tool "llvm"; $script:cmakeDir = Get-Tool "cmake"; $script:ninjaDir = Get-Tool "ninja"
        $script:python = Find-Python
        $global:LASTEXITCODE = 0
    }
    $env:PATH = "$llvm\bin;$cmakeDir\bin;$ninjaDir;$env:PATH"
}

# ---------------------------------------------------------------- prepare, configure, build
if (-not $SkipPrepare) {
    $prepareArgs = @("tools/prepare.py")
    if ($Iso) { $prepareArgs += $Iso }
    if (-not $ForceWideScreen) { $prepareArgs += "--no-widescreen" }
    Step "python $($prepareArgs -join ' ')" { & $python @prepareArgs }
}

# a build folder made by the other toolchain is started again
$generator = if ($VisualStudio) { "Visual Studio 18 2026" } else { "Ninja" }
$cache = Join-Path $PSScriptRoot "out\CMakeCache.txt"
if ((Test-Path $cache) -and -not (Select-String -Path $cache -SimpleMatch -Quiet "CMAKE_GENERATOR:INTERNAL=$generator")) {
    Write-Host "  out\ was made by another toolchain: starting it again" -ForegroundColor Yellow
    Remove-Item (Join-Path $PSScriptRoot "out") -Recurse -Force
}

if ($VisualStudio) {
    Step "cmake configure (Visual Studio)" { cmake -B out -G $generator -A x64 }
    Step "cmake build ($Config)" { cmake --build out --config $Config }
} else {
    Step "cmake configure (LLVM-MinGW)" {
        cmake -B out -G Ninja "-DCMAKE_BUILD_TYPE=$Config" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
            "-DCMAKE_RC_COMPILER=$llvm\bin\x86_64-w64-mingw32-windres.exe"
    }
    Step "cmake build ($Config)" { cmake --build out }
}

$timer.Stop()
$what = if ($isClone) { "'" + (git branch --show-current) + "'" } else { "this folder" }
Write-Host ""
Write-Host ("Build succeeded on {0} in {1:mm\:ss}: out\{2}\sfa3r.exe" -f $what, $timer.Elapsed, $Config) -ForegroundColor Green

if ($Run) {
    Step "run sfa3r" { & ".\out\$Config\sfa3r.exe" --work work }
}
