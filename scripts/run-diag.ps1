$root  = Split-Path -Parent $PSScriptRoot
$out   = Join-Path $root 'logs/client.out.txt'
$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'

Get-Process kernmem-loader, kernmem-cs2 -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
}
Remove-Item $out, $state -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600

Start-Process -FilePath (Join-Path $root 'dist\kernmem-cs2.exe') `
    -WorkingDirectory (Join-Path $root 'dist') `
    -RedirectStandardOutput $out | Out-Null

Start-Sleep -Seconds 45

'=== client stdout ==='
if (Test-Path $out) { Get-Content $out } else { 'no output file' }

'=== state ==='
if (Test-Path $state) { Get-Content $state | ForEach-Object { "[$_]" } } else { 'missing' }

'=== procs ==='
Get-Process kernmem-loader, kernmem-cs2 -ErrorAction SilentlyContinue |
    Select-Object Name, Id | Format-Table -AutoSize | Out-String -Width 60

Get-Process kernmem-loader, kernmem-cs2 -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
}
