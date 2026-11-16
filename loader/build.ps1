# Builds kernmem-loader.exe.
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
foreach ($h in @('km_sys.h', 'iqvw_sys.h')) {
    $p = Join-Path $resDir $h
    if (-not (Test-Path $p)) { throw "missing embedded resource: $p (run build.ps1 from the repo root)" }
}

# The res headers are committed so a clone builds, which means they can go
# stale. Timestamps are useless here (a clone stamps every file with clone
# time), so the hash stamped by tools/embed.ps1 is compared instead. The check
# is skipped when build/km.sys is absent, which is exactly the fresh-clone case.
$builtDriver = Join-Path $PSScriptRoot '../build/km.sys'

if (Test-Path $builtDriver) {
    $declared = (Select-String -Path (Join-Path $resDir 'km_sys.h') `
                 -Pattern '^#define km_sys_SHA256 "([0-9A-Fa-f]{64})"').Matches

    if (-not $declared.Count) {
        throw 'loader/res/km_sys.h carries no source hash - re-run build.ps1 from the repo root'
    }

    $actual = (Get-FileHash $builtDriver -Algorithm SHA256).Hash

    if ($declared[0].Groups[1].Value.ToUpperInvariant() -ne $actual) {
        throw 'loader/res/km_sys.h embeds a different driver than build/km.sys - re-run build.ps1 from the repo root'
    }

    Write-Host 'embedded driver matches build/km.sys'
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$exe = Join-Path $OutDir 'kernmem-loader.exe'

$sources = @('main.cpp', 'nal.cpp', 'nt.cpp', 'service.cpp', 'log.cpp', 'pe.cpp', 'mapper.cpp', 'game.cpp', 'trace.cpp') |
           ForEach-Object { "`"$PSScriptRoot\$_`"" }

$cl = @(
    '/nologo', '/c', '/std:c++17', '/EHsc', '/O2', '/MT', '/W3', '/wd4201', '/D_WIN32_WINNT=0x0A00',
    "/I`"$umInc`"", "/I`"$shared`"", "/I`"$ucrt`"", "/I`"$PSScriptRoot`"",
    "/Fo`"$OutDir\\`""
) + $sources

$objs = @('main.obj', 'nal.obj', 'nt.obj', 'service.obj', 'log.obj', 'pe.obj', 'mapper.obj', 'game.obj', 'trace.obj') |
        ForEach-Object { "`"$OutDir\$_`"" }

$link = @(
    '/nologo', '/SUBSYSTEM:CONSOLE', '/MACHINE:X64', '/OPT:REF', '/OPT:ICF',
    "/OUT:`"$exe`"", "/LIBPATH:`"$umLib`""
) + $objs + @('ntdll.lib', 'advapi32.lib', 'gdi32.lib', 'user32.lib', 'shell32.lib', 'psapi.lib')

$cmd = "call `"$vcvars`" >nul 2>&1 && cl $($cl -join ' ') && link $($link -join ' ')"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "loader build failed with exit code $LASTEXITCODE" }

if (-not (Test-Path $exe)) { throw 'link produced no output' }

Write-Host "built $exe ($((Get-Item $exe).Length) bytes)"
