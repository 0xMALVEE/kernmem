# Full build: driver -> safety gate -> embed -> loader -> dist.
#
# A build is only green when dist/vgb-loader.exe exists AND the driver passed
# the mapper-safety check. The gate is not advisory: an image with a foreign
# import or no relocations cannot be manually mapped.

param(
    [switch]$SkipDriver,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$buildDir = Join-Path $root 'build'
$distDir  = Join-Path $root 'dist'
$resDir   = Join-Path $root 'loader/res'
$driverSys = Join-Path $buildDir 'vgb.sys'
$iqvwSys   = Join-Path $distDir 'iqvw64e.sys'

if ($Clean) {
    foreach ($d in @($buildDir, $distDir, $resDir)) {
        if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    }
    Write-Host 'cleaned'
    return
}

$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

# NOTE: the switch parameter must not be called $args - that is a PowerShell
# automatic variable and the value would be lost.
function Invoke-Dumpbin([string]$file, [string]$switch) {
    $cmd = "call `"$vcvars`" >nul 2>&1 && dumpbin $switch `"$file`""
    return (cmd /c $cmd 2>&1 | Out-String)
}

New-Item -ItemType Directory -Force -Path $buildDir, $distDir, $resDir | Out-Null

# ---------------------------------------------------------------- driver ---
if (-not $SkipDriver) {
    Write-Host '=== driver ==='
    & powershell -ExecutionPolicy Bypass -File (Join-Path $root 'driver/build.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'driver build failed' }
}
elseif (-not (Test-Path $driverSys)) {
    throw '-SkipDriver given but build/vgb.sys does not exist'
}

# ----------------------------------------------------------- safety gate ---
Write-Host '=== mapper-safety gate ==='
$imports = Invoke-Dumpbin $driverSys '/imports'

if ($imports -notmatch 'Section contains the following imports') {
    throw 'dumpbin produced no import table - the image or the toolchain is wrong'
}

$providers = [regex]::Matches($imports, '(?m)^\s+([A-Za-z0-9_\-]+\.(?:exe|dll|sys))\s*$') |
             ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique

if (-not $providers) { throw 'could not parse any import provider' }

foreach ($p in $providers) {
    if ($p -ne 'ntoskrnl.exe') {
        throw "driver imports from $p - only ntoskrnl.exe is allowed"
    }
}

$relocs = Invoke-Dumpbin $driverSys '/relocations'
if ($relocs -notmatch '(?m)^BASE RELOCATIONS') {
    throw 'driver has no relocation section - it cannot be mapped'
}

if ($imports -match '__security_cookie') { throw 'driver has a /GS cookie; it must be built with /GS-' }

Write-Host "gate passed: imports=$($providers -join ','), relocations present, no cookie"

# ------------------------------------------------------- contract parity ---
$driverHeader = (Get-FileHash (Join-Path $root 'driver/vgb_shared.h') -Algorithm SHA256).Hash
$sdkHeader    = (Get-FileHash (Join-Path $root 'sdk/vgb_shared.h') -Algorithm SHA256).Hash

if ($driverHeader -ne $sdkHeader) {
    throw 'driver/vgb_shared.h and sdk/vgb_shared.h have diverged - keep them identical'
}

Write-Host 'contract parity ok'

# ----------------------------------------------------------------- embed ---
Write-Host '=== embed ==='
& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'tools/embed.ps1') `
    -InputFile $driverSys -OutputFile (Join-Path $resDir 'vgb_sys.h') -Symbol 'vgb_sys'
if ($LASTEXITCODE -ne 0) { throw 'embedding vgb.sys failed' }

if (-not (Test-Path $iqvwSys)) {
    throw "missing $iqvwSys - the signed mapping host must be present in dist/"
}

& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'tools/embed.ps1') `
    -InputFile $iqvwSys -OutputFile (Join-Path $resDir 'iqvw_sys.h') -Symbol 'iqvw64e_sys'
if ($LASTEXITCODE -ne 0) { throw 'embedding iqvw64e.sys failed' }

# ---------------------------------------------------------------- loader ---
Write-Host '=== loader ==='
& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'loader/build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'loader build failed' }

# ------------------------------------------------------------------ dist ---
Write-Host '=== dist ==='
# ------------------------------------------------------------------- ui ---
# The shipped executable is built with the interactive menu as its default
# entry point; the CLI verbs remain available for scripting.
Write-Host '=== example client ==='
& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'client/build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'example client build failed' }

Copy-Item (Join-Path $buildDir 'vgb-loader.exe') $distDir -Force
Copy-Item (Join-Path $buildDir 'vgb-example.exe') $distDir -Force

foreach ($d in @('docs', 'sdk', 'client', 'scripts')) {
    $src = Join-Path $root $d
    if (Test-Path $src) {
        $target = Join-Path $distDir $d
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item "$src\*" $target -Recurse -Force
    }
}

# Python bytecode caches are machine-specific and must not ship.
Get-ChildItem $distDir -Directory -Recurse -Filter '__pycache__' | Remove-Item -Recurse -Force

foreach ($f in @('README.md', 'LICENSE')) {
    $src = Join-Path $root $f
    if (Test-Path $src) { Copy-Item $src $distDir -Force }
}

# The manifest must not include itself: it would be enumerated and then locked
# by the write while Get-FileHash still reads it. Hashes are collected first and
# the file is written atomically.
$manifest = Join-Path $distDir 'SHA256SUMS.txt'
$lines = @()

foreach ($file in Get-ChildItem $distDir -File -Recurse) {
    if ($file.Name -eq 'SHA256SUMS.txt') { continue }

    $hash = (Get-FileHash $file.FullName -Algorithm SHA256).Hash
    $lines += "$hash  $($file.FullName.Substring($distDir.Length + 1))"
}

$temp = "$manifest.new"
Set-Content -Path $temp -Value $lines
Move-Item -Path $temp -Destination $manifest -Force

Write-Host "manifest: $($lines.Count) files"

Write-Host ''
Write-Host 'build green:'
Get-ChildItem $distDir | Select-Object Name, Length | Format-Table -Auto
