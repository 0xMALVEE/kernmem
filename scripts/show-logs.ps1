$logDir = Join-Path ([IO.Path]::GetTempPath()) 'km-logs'

Get-ChildItem $logDir -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 8 |
    ForEach-Object {
        $isClient = Select-String -Path $_.FullName -Pattern 'client:' -Quiet -ErrorAction SilentlyContinue
        if ($isClient) {
            "=== $($_.Name) ($($_.Length) bytes) ==="
            Get-Content $_.FullName
        }
    }

'=== procs ==='
Get-Process kernmem-cs2, kernmem-loader, cs2 -ErrorAction SilentlyContinue |
    Select-Object Name, Id, MainWindowHandle, Responding | Format-Table -AutoSize | Out-String -Width 100
