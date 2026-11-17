$root  = $PSScriptRoot
$logDir = Join-Path ([IO.Path]::GetTempPath()) 'km-logs'
$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'

Get-Process kernmem-loader, kernmem-cs2 -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
}
Remove-Item $state -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600

$before = @(Get-ChildItem $logDir -File -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)

Start-Process -FilePath (Join-Path $root 'dist\kernmem-cs2.exe') -WorkingDirectory (Join-Path $root 'dist') | Out-Null

Start-Sleep -Seconds 25

'=== processes ==='
Get-Process kernmem-cs2, kernmem-loader -ErrorAction SilentlyContinue |
    Select-Object Name, Id, MainWindowHandle, Responding | Format-Table -AutoSize | Out-String -Width 100

'=== client log ==='
$logs = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue |
    Where-Object { $before -notcontains $_.Name }

foreach ($f in $logs) {
    if (Select-String -Path $f.FullName -Pattern 'client:' -Quiet -ErrorAction SilentlyContinue) {
        "--- $($f.Name) ---"
        Get-Content $f.FullName
    }
}

'=== loader log tail ==='
$newest = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1

if ($newest) { Get-Content $newest.FullName -Tail 8 }
