$root  = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path ([IO.Path]::GetTempPath()) 'km-logs'
$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'

Get-Process kernmem-loader, kernmem-cs2 -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
}
Remove-Item $state -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600

$before = @(Get-ChildItem $logDir -File -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)

Start-Process -FilePath (Join-Path $root 'dist\kernmem-cs2.exe') -WorkingDirectory (Join-Path $root 'dist') | Out-Null

for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Seconds 1

    $alive = (Get-Process kernmem-cs2 -ErrorAction SilentlyContinue | Measure-Object).Count
    $loader = (Get-Process kernmem-loader -ErrorAction SilentlyContinue | Measure-Object).Count
    "[$i s] client=$alive loader=$loader"

    if ($alive -eq 0) { break }
}

'=== new log files ==='
$after = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue |
    Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime

foreach ($f in $after) {
    "--- $($f.Name) ($($f.Length) bytes) ---"
    Get-Content $f.FullName -ErrorAction SilentlyContinue
}

'=== newest log overall ==='
$newest = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1

if ($newest) {
    "--- $($newest.Name) ---"
    Get-Content $newest.FullName -ErrorAction SilentlyContinue
}

'=== state ==='
if (Test-Path $state) { Get-Content $state | ForEach-Object { "[$_]" } } else { 'missing' }
