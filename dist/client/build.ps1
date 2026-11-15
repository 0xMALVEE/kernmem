# Builds the example client against the SDK.

param([string]$OutDir = "$PSScriptRoot/../build")

$ErrorActionPreference = 'Stop'

$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVer  = '10.0.26100.0'
$umInc   = "$sdkRoot\Include\$sdkVer\um"
$shared  = "$sdkRoot\Include\$sdkVer\shared"
$ucrt    = "$sdkRoot\Include\$sdkVer\ucrt"
$umLib   = "$sdkRoot\Lib\$sdkVer\um\x64"
$vcvars  = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$exe = Join-Path $OutDir 'vgb-example.exe'

$cmd = @(
    "call `"$vcvars`" >nul 2>&1",
    '&& cl /nologo /std:c++17 /EHsc /O2 /MT /W3 /D_WIN32_WINNT=0x0A00',
    "/I`"$umInc`" /I`"$shared`" /I`"$ucrt`" /I`"$PSScriptRoot/../sdk`"",
    "/Fe`"$exe`" `"$PSScriptRoot/example.cpp`"",
    "/link /SUBSYSTEM:CONSOLE /LIBPATH:`"$umLib`" ntdll.lib advapi32.lib"
) -join ' '

cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "example build failed with exit code $LASTEXITCODE" }

Write-Host "built $exe"
