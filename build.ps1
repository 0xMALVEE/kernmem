# Full build: driver -> safety gate -> embed -> loader -> dist.
#
# A build is only green when dist/kernmem-loader.exe exists AND the driver passed
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
$driverSys = Join-Path $buildDir 'km.sys'
$iqvwSys   = Join-Path $distDir 'iqvw64e.sys'

if ($Clean) {
    # dist/iqvw64e.sys is a tracked INPUT (the signed mapping host), not build
    # output, and it lives in a directory that has to be wiped. Park it and put it
    # back, or every clean build dies at the embed step.
    $hostSys = Join-Path $distDir 'iqvw64e.sys'
    # Parked at the repo root, NOT under build/ - both build/ and dist/ are wiped
    # by the loop below.
    $hostBackup = Join-Path $root '.iqvw64e.sys.keep'

    # build/*.py are the committed offset scanners, not compiler output. They are
    # kept in build/ by .gitignore exception, so wiping the directory deletes
    # tracked files unless they are parked too.
    $pyBackup = Join-Path $root '.build-py.keep'
    $pyFiles = @(Get-ChildItem -Path $buildDir -Filter '*.py' -File -ErrorAction SilentlyContinue)

    if (Test-Path $hostSys) {
        Copy-Item $hostSys $hostBackup -Force
    }

    if ($pyFiles.Count -gt 0) {
        New-Item -ItemType Directory -Force -Path $pyBackup | Out-Null
        foreach ($f in $pyFiles) { Copy-Item $f.FullName $pyBackup -Force }
    }

    #
    # The restore runs in a finally: a running client holds its exe open, and one
    # Access-denied used to abort the wipe and leave the tracked files deleted.
    #
    try {
        foreach ($d in @($buildDir, $distDir, $resDir)) {
            if (-not (Test-Path $d)) { continue }

            # Rename anything locked out of the way first, so the recursive delete
            # can proceed. The leftovers are cleaned on a later run.
            foreach ($f in @(Get-ChildItem -Path $d -File -Recurse -ErrorAction SilentlyContinue)) {
                try {
                    $stream = [System.IO.File]::Open($f.FullName, 'Open', 'ReadWrite', 'None')
                    $stream.Close()
                }
                catch {
                    try { Move-Item $f.FullName ($f.FullName + '.old') -Force -ErrorAction Stop } catch { }
                }
            }

            Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
        }
    }
    finally {
        if (Test-Path $hostBackup) {
            New-Item -ItemType Directory -Force -Path $distDir | Out-Null
            Move-Item $hostBackup $hostSys -Force
        }

        if (Test-Path $pyBackup) {
            New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
            Get-ChildItem -Path $pyBackup -File | ForEach-Object { Move-Item $_.FullName (Join-Path $buildDir $_.Name) -Force }
            Remove-Item $pyBackup -Recurse -Force
        }
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

# A loader left running holds dist\kernmem-loader.exe open, and Copy-Item then
# fails the whole build on the last step. Retry briefly instead: the file is
# replaceable as soon as the process exits.
function Copy-Retry([string]$source, [string]$target) {
    for ($attempt = 0; $attempt -lt 10; ++$attempt) {
        try {
            Copy-Item $source $target -Force -ErrorAction Stop
            return
        }
        catch {
            if ($attempt -eq 9) {
                Write-Host "  locked, left the previous copy in place: $(Split-Path -Leaf $target)"
                return
            }
            Start-Sleep -Milliseconds 400
        }
    }
}

New-Item -ItemType Directory -Force -Path $buildDir, $distDir, $resDir | Out-Null

# ---------------------------------------------------------------- driver ---
if (-not $SkipDriver) {
    Write-Host '=== driver ==='
    & powershell -ExecutionPolicy Bypass -File (Join-Path $root 'driver/build.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'driver build failed' }
}
elseif (-not (Test-Path $driverSys)) {
    throw '-SkipDriver given but build/km.sys does not exist'
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
$driverHeader = (Get-FileHash (Join-Path $root 'driver/km_shared.h') -Algorithm SHA256).Hash
$sdkHeader    = (Get-FileHash (Join-Path $root 'sdk/km_shared.h') -Algorithm SHA256).Hash

if ($driverHeader -ne $sdkHeader) {
    throw 'driver/km_shared.h and sdk/km_shared.h have diverged - keep them identical'
}

Write-Host 'contract parity ok'

# ----------------------------------------------------------------- embed ---
Write-Host '=== embed ==='
& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'tools/embed.ps1') `
    -InputFile $driverSys -OutputFile (Join-Path $resDir 'km_sys.h') -Symbol 'km_sys'
if ($LASTEXITCODE -ne 0) { throw 'embedding km.sys failed' }

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
Write-Host '=== league client ==='
& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'league/client/build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'league client build failed' }

# The CS2 client is the reference for how a per-game client is laid out, so it is
# built on every run rather than on demand - a broken reference is worse than a
# missing one.
Write-Host '=== cs2 client ==='
& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'cs2/client/build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'cs2 client build failed' }

# Apex has its own dump in apex-legends/offsets.txt and its own provider; it is
# built on every run for the same reason as CS2 - a client that silently stops
# building is worse than one that fails loudly here.
Write-Host '=== apex client ==='
& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'apex-legends/client/build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'apex client build failed' }

# A locked binary gets renamed aside by the retry path, so drop the leftovers
# rather than letting them pile up in dist. Ones still held stay until next run.
Get-ChildItem -Path $distDir -Filter '*.old*' -File -ErrorAction SilentlyContinue |
    ForEach-Object { try { Remove-Item $_.FullName -Force -ErrorAction Stop } catch { } }

Copy-Retry (Join-Path $buildDir 'kernmem-loader.exe') (Join-Path $distDir 'kernmem-loader.exe')
Copy-Retry (Join-Path $buildDir 'kernmem-league.exe') (Join-Path $distDir 'kernmem-league.exe')
Copy-Retry (Join-Path $buildDir 'kernmem-cs2.exe') (Join-Path $distDir 'kernmem-cs2.exe')
Copy-Retry (Join-Path $buildDir 'kernmem-apex.exe') (Join-Path $distDir 'kernmem-apex.exe')

# cs2/cheat-referance is a vendored third-party project, not ours - shipping it
# into dist doubles the bundle for no benefit.
foreach ($d in @('docs', 'sdk', 'tools', 'league', 'valorant', 'cs2', 'apex-legends')) {
    $src = Join-Path $root $d
    if (Test-Path $src) {
        $target = Join-Path $distDir $d
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item "$src\*" $target -Recurse -Force -Exclude 'cheat-referance'
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

    # *.old* is the retry path's scratch copy of a locked binary, not shipped.
    if ($file.Name -match '\.old') { continue }

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
