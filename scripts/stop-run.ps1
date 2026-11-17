Get-Process kernmem-cs2, kernmem-loader -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
}

Start-Sleep -Milliseconds 900

'remaining: '
Get-Process kernmem-cs2, kernmem-loader -ErrorAction SilentlyContinue |
    Select-Object Name, Id | Format-Table -AutoSize | Out-String -Width 60
