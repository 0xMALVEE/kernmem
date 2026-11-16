# Builds km.sys as a manually-mappable kernel image.
# Requires the WDK headers/libs that the workspace already provisioned.

param(
    [string]$Configuration = 'Release',
    [string]$OutDir = "$PSScriptRoot/../build"
)

$ErrorActionPreference = 'Stop'

$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVer  = '10.0.26100.0'
$kmInc   = "$sdkRoot\Include\$sdkVer\km"
$shared  = "$sdkRoot\Include\$sdkVer\shared"
$ucrt    = "$sdkRoot\Include\$sdkVer\ucrt"
$kmLib   = "$sdkRoot\Lib\$sdkVer\km\x64"
$vcvars  = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

foreach ($p in @($kmInc, $kmLib, $vcvars)) {
    if (-not (Test-Path $p)) { throw "Missing required toolchain path: $p" }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$obj = Join-Path $OutDir 'km.obj'
$sys = Join-Path $OutDir 'km.sys'

$cl = @(
    '/nologo', '/c', '/kernel', '/GS-', '/Zl', '/Oi', '/O2', '/W3', '/wd4201', '/D_AMD64_', '/DAMD64', '/D_KERNEL_MODE',
    "/I`"$kmInc`"", "/I`"$shared`"", "/I`"$ucrt`"", "/I`"$PSScriptRoot`"",
    "/Fo`"$obj`"",
    "`"$PSScriptRoot\main.c`""
) -join ' '

$link = @(
    '/nologo', '/DRIVER', '/SUBSYSTEM:NATIVE', '/MACHINE:X64',
    '/ENTRY:DriverEntry', '/OPT:REF', '/OPT:ICF',
    "/OUT:`"$sys`"", "/LIBPATH:`"$kmLib`"",
    "`"$obj`"",
    'ntoskrnl.lib', 'BufferOverflowK.lib', 'wdm.lib'
) -join ' '

$cmd = "call `"$vcvars`" >nul 2>&1 && cl $cl && link $link"
Write-Host $cmd
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }

if (-not (Test-Path $sys)) { throw 'link produced no output' }

$item = Get-Item $sys
Write-Host "built $($item.FullName) ($($item.Length) bytes)"
