# Builds vgb-loader.exe.
#
# The loader is statically linked (/MT) so dist/ has no redistributable
# dependency and runs on a clean machine with no toolchain installed.

param(
    [string]$Configuration = 'Release',
    [string]$OutDir = "$PSScriptRoot/../build"
)

$ErrorActionPreference = 'Stop'

$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVer  = '10.0.26100.0'
$umInc   = "$sdkRoot\Include\$sdkVer\um"
$shared  = "$sdkRoot\Include\$sdkVer\shared"
$ucrt    = "$sdkRoot\Include\$sdkVer\ucrt"
$umLib   = "$sdkRoot\Lib\$sdkVer\um\x64"
$vcvars  = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

foreach ($p in @($umInc, $umLib, $vcvars)) {
    if (-not (Test-Path $p)) { throw "Missing required toolchain path: $p" }
}

$resDir = Join-Path $PSScriptRoot 'res'
foreach ($h in @('vgb_sys.h', 'iqvw_sys.h')) {
    $p = Join-Path $resDir $h
    if (-not (Test-Path $p)) { throw "missing embedded resource: $p (run build.ps1 from the repo root)" }
}

# The res headers are committed so a clone builds, which means they can go
# stale. Refuse to link an embedded driver older than the driver sources.
$embedded = Get-Item (Join-Path $resDir 'vgb_sys.h')
$driverSources = Get-ChildItem (Join-Path $PSScriptRoot '../driver') -File |
                 Where-Object { $_.Extension -in @('.c', '.h') }

foreach ($source in $driverSources) {
    if ($source.LastWriteTime -gt $embedded.LastWriteTime) {
        throw "$($source.Name) is newer than loader/res/vgb_sys.h - run build.ps1 from the repo root so the embedded driver is regenerated"
    }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$exe = Join-Path $OutDir 'vgb-loader.exe'

$sources = @('main.cpp', 'nal.cpp', 'nt.cpp', 'service.cpp', 'log.cpp', 'pe.cpp', 'mapper.cpp') |
           ForEach-Object { "`"$PSScriptRoot\$_`"" }

$cl = @(
    '/nologo', '/c', '/std:c++17', '/EHsc', '/O2', '/MT', '/W3', '/wd4201', '/D_WIN32_WINNT=0x0A00',
    "/I`"$umInc`"", "/I`"$shared`"", "/I`"$ucrt`"", "/I`"$PSScriptRoot`"",
    "/Fo`"$OutDir\\`""
) + $sources

$objs = @('main.obj', 'nal.obj', 'nt.obj', 'service.obj', 'log.obj', 'pe.obj', 'mapper.obj') |
        ForEach-Object { "`"$OutDir\$_`"" }

$link = @(
    '/nologo', '/SUBSYSTEM:CONSOLE', '/MACHINE:X64', '/OPT:REF', '/OPT:ICF',
    "/OUT:`"$exe`"", "/LIBPATH:`"$umLib`""
) + $objs + @('ntdll.lib', 'advapi32.lib')

$cmd = "call `"$vcvars`" >nul 2>&1 && cl $($cl -join ' ') && link $($link -join ' ')"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "loader build failed with exit code $LASTEXITCODE" }

if (-not (Test-Path $exe)) { throw 'link produced no output' }

Write-Host "built $exe ($((Get-Item $exe).Length) bytes)"
