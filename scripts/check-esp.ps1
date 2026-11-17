$root  = Split-Path -Parent $PSScriptRoot
$out   = Join-Path $root 'logs/client.out.txt'
$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'

$sig = @'
[DllImport("user32")]
public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32")]
public static extern bool ShowWindow(IntPtr hWnd, int cmd);
'@

$u32 = (Add-Type -MemberDefinition $sig -Name Fg -Namespace FG -PassThru)[0]

Get-Process kernmem-loader, kernmem-cs2 -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
}
Remove-Item $out, $state -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800

$cs2 = Get-Process cs2 -ErrorAction SilentlyContinue | Select-Object -First 1
'cs2: ' + $(if ($cs2) { $cs2.Id } else { 'not running' })

Start-Process -FilePath (Join-Path $root 'dist\kernmem-cs2.exe') `
    -WorkingDirectory (Join-Path $root 'dist') `
    -RedirectStandardOutput $out | Out-Null

Start-Sleep -Seconds 35

if ($cs2) {
    [void]$u32::ShowWindow($cs2.MainWindowHandle, 9)
    [void]$u32::SetForegroundWindow($cs2.MainWindowHandle)
    Start-Sleep -Seconds 6
}

'=== client output tail ==='
if (Test-Path $out) { Get-Content $out -Tail 25 } else { 'no output file' }

'=== overlay window ==='
$win = Join-Path $root 'win-probe.ps1'
if (Test-Path $win) { & powershell -NoProfile -ExecutionPolicy Bypass -File $win }
