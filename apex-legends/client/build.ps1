# Builds the Apex Legends client.
#
# The game only supplies offsets and a provider; the overlay, drawing and frame
# loop come from the shared framework in sdk/client, which is compiled in here.

param([string]$OutDir = "$PSScriptRoot/../../build")

$ErrorActionPreference = 'Stop'

$sdkRoot = 'C:/Program Files (x86)/Windows Kits/10'
$sdkVer  = '10.0.26100.0'
$umInc   = "$sdkRoot/Include/$sdkVer/um"
$shared  = "$sdkRoot/Include/$sdkVer/shared"
$ucrt    = "$sdkRoot/Include/$sdkVer/ucrt"
$umLib   = "$sdkRoot/Lib/$sdkVer/um/x64"
$vcvars  = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$exe = Join-Path $OutDir 'kernmem-apex.exe'

# Framework first, then the game. Adding a game copies these two lines.
$framework = @('esp.cpp', 'overlay.cpp') |
             ForEach-Object { "`"$PSScriptRoot/../../sdk/client/$_`"" }

$sources = @('main.cpp', 'provider.cpp') |
           ForEach-Object { "`"$PSScriptRoot/$_`"" }

$cl = @(
    '/nologo', '/std:c++17', '/EHsc', '/O2', '/MT', '/W3', '/wd4201', '/D_WIN32_WINNT=0x0A00',
    "/I`"$umInc`"", "/I`"$shared`"", "/I`"$ucrt`"", "/I`"$PSScriptRoot/../../sdk`"",
    "/I`"$PSScriptRoot/../../sdk/client`"", "/Fo`"$OutDir\\`"", "/Fe`"$exe`""
) + $framework + $sources

$link = @(
    '/link', '/SUBSYSTEM:CONSOLE', '/MACHINE:X64', "/LIBPATH:`"$umLib`"",
    'ntdll.lib', 'advapi32.lib', 'gdi32.lib', 'user32.lib'
)

$cmd = "call `"$vcvars`" >nul 2>&1 && cl $($cl -join ' ') $($link -join ' ')"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "apex client build failed with exit code $LASTEXITCODE" }

if (-not (Test-Path $exe)) { throw 'link produced no output' }

Write-Host "built $exe ($((Get-Item $exe).Length) bytes)"
