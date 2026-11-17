$logDir = Join-Path ([IO.Path]::GetTempPath()) 'km-logs'

$newest = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $newest) { 'no logs'; exit 0 }

"newest: $($newest.Name)  ($($newest.Length) bytes)"

Get-Content $newest.FullName -Tail 8

'--- pid lines ---'
Select-String -Path $newest.FullName -Pattern 'client pid|registered' |
    ForEach-Object { $_.Line }

'--- client log ---'
Get-ChildItem $logDir -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 8 |
    ForEach-Object {
        if (Select-String -Path $_.FullName -Pattern 'client:' -Quiet -ErrorAction SilentlyContinue) {
            "=== $($_.Name) ==="
            Get-Content $_.FullName
        }
    }

'--- procs ---'
Get-Process kernmem-cs2, kernmem-loader, cs2 -ErrorAction SilentlyContinue |
    Select-Object Name, Id, Responding | Format-Table -AutoSize | Out-String -Width 90
