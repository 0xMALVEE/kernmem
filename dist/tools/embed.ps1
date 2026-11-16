# Embeds a binary file as a C byte array header.
#
# Used to bake km.sys and iqvw64e.sys into the loader, so the shipped exe is
# self-contained and a user cannot pair a stale loader with a fresh driver.

param(
    [Parameter(Mandatory = $true)][string]$InputFile,
    [Parameter(Mandatory = $true)][string]$OutputFile,
    [Parameter(Mandatory = $true)][string]$Symbol
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $InputFile)) { throw "missing input: $InputFile" }

$bytes = [System.IO.File]::ReadAllBytes($InputFile)
if ($bytes.Length -eq 0) { throw "input is empty: $InputFile" }

$outDir = Split-Path -Parent $OutputFile
if ($outDir) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

# The source hash is stamped into the header so a committed copy can be proven
# to match the file it was generated from, without relying on timestamps.
$sourceHash = (Get-FileHash $InputFile -Algorithm SHA256).Hash

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('#pragma once')
[void]$sb.AppendLine("/* generated from $(Split-Path -Leaf $InputFile) - do not edit */")
[void]$sb.AppendLine("#define ${Symbol}_SHA256 `"$sourceHash`"")
[void]$sb.AppendLine("static const unsigned char ${Symbol}[] = {")

for ($i = 0; $i -lt $bytes.Length; $i += 16) {
    $last = [Math]::Min($i + 15, $bytes.Length - 1)
    $line = ($bytes[$i..$last] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ','
    [void]$sb.AppendLine("    $line,")
}

[void]$sb.AppendLine('};')
[void]$sb.AppendLine("static const unsigned long ${Symbol}_size = $($bytes.Length)UL;")

[System.IO.File]::WriteAllText($OutputFile, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
Write-Host "embedded $($bytes.Length) bytes -> $OutputFile ($Symbol)"
