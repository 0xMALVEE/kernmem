# Refreshes the shipped binaries in dist/ after a build whose copy step was
# skipped because an elevated loader held the target file.
#
# Mirrors build.ps1: a locked target is renamed aside and retried, *.old
# leftovers are dropped, and SHA256SUMS.txt is rebuilt atomically.

$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build'
$distDir  = Join-Path $root 'dist'

# The stuck loader is what holds the file. Nothing else depends on it here.
Get-Process kernmem-loader -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
}
Start-Sleep -Milliseconds 800

foreach ($name in @('kernmem-loader.exe', 'kernmem-league.exe', 'kernmem-cs2.exe')) {
    $src = Join-Path $buildDir $name
    $dst = Join-Path $distDir $name

    if (-not (Test-Path $src)) { continue }

    try {
        Copy-Item $src $dst -Force -ErrorAction Stop
    }
    catch {
        $old = "$dst.old"
        try { Remove-Item $old -Force -ErrorAction SilentlyContinue } catch { }
        Move-Item $dst $old -Force
        Copy-Item $src $dst -Force
        Write-Host "renamed aside: $name"
    }
}

Get-ChildItem -Path $distDir -Filter '*.old*' -File -ErrorAction SilentlyContinue |
    ForEach-Object { try { Remove-Item $_.FullName -Force -ErrorAction Stop } catch { } }

$manifest = Join-Path $distDir 'SHA256SUMS.txt'
$lines = @()

foreach ($file in Get-ChildItem $distDir -File -Recurse) {
    if ($file.Name -eq 'SHA256SUMS.txt') { continue }
    if ($file.Name -match '\.old') { continue }

    $hash = (Get-FileHash $file.FullName -Algorithm SHA256).Hash
    $lines += "$hash  $($file.FullName.Substring($distDir.Length + 1))"
}

$temp = "$manifest.new"
Set-Content -Path $temp -Value $lines
Move-Item -Path $temp -Destination $manifest -Force

Write-Host "refreshed dist; manifest: $($lines.Count) files"
