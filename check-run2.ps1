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

for ($i = 0; $i -lt 12; $i++) {
    Start-Sleep -Seconds 5

    $proc = Get-Process kernmem-cs2 -ErrorAction SilentlyContinue
    $win  = 0
    if ($proc) { $win = $proc.MainWindowHandle }

    "[$((($i + 1) * 5)) s] alive=$([bool]$proc) win=$win"

    if (-not $proc) { break }
}

'=== client logs ==='
$logs = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue |
    Where-Object { $before -notcontains $_.Name }

foreach ($f in $logs) {
    "--- $($f.Name) ($($f.Length) bytes) ---"
    Get-Content $f.FullName -ErrorAction SilentlyContinue
}

'=== state ==='
if (Test-Path $state) { Get-Content $state | ForEach-Object { "[$_]" } } else { 'missing' }
