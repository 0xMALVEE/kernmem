$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root 'dist\kernmem-cs2.exe'
$out  = Join-Path $root 'logs/client.out.txt'
$err  = Join-Path $root 'logs/client.err.txt'
$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'

Remove-Item $out, $err -Force -ErrorAction SilentlyContinue
Remove-Item $state -Force -ErrorAction SilentlyContinue

Get-Process kernmem-loader, kernmem-cs2 -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
}
Start-Sleep -Milliseconds 500

$before = (Get-ChildItem (Join-Path ([IO.Path]::GetTempPath()) 'km-logs') -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1).Name

Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) `
    -RedirectStandardOutput $out -RedirectStandardError $err | Out-Null

Start-Sleep -Seconds 30

'=== client stdout ==='
Get-Content $out -ErrorAction SilentlyContinue
'=== client stderr ==='
Get-Content $err -ErrorAction SilentlyContinue

'=== processes ==='
Get-Process kernmem-loader, kernmem-cs2 -ErrorAction SilentlyContinue |
    Select-Object Name, Id | Format-Table -AutoSize | Out-String -Width 80

'=== state file ==='
if (Test-Path $state) { Get-Content $state | ForEach-Object { "[$_]" } } else { 'missing' }

'=== new logs ==='
Get-ChildItem (Join-Path ([IO.Path]::GetTempPath()) 'km-logs') -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 3 Name, Length | Format-Table -AutoSize | Out-String -Width 80
