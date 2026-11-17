$sig = @'
[DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
[DllImport("kernel32", SetLastError=true)]
public static extern bool CloseHandle(IntPtr h);
'@

$k32 = Add-Type -MemberDefinition $sig -Name K32f -Namespace W6 -PassThru
$read  = [uint32]2147483648
$share = [uint32]3
$open  = [uint32]3

function Try-Open([string]$path) {
    $h = $k32::CreateFileW($path, $read, $share, [IntPtr]::Zero, $open, [uint32]0, [IntPtr]::Zero)

    if ($h.ToInt64() -eq -1) {
        return "FAIL($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
    }

    [void]$k32::CloseHandle($h)
    return 'OK'
}

$root  = Split-Path -Parent $PSScriptRoot
$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'

Get-Process kernmem-loader, kernmem-cs2 -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
}
Remove-Item $state -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600

Start-Process -FilePath (Join-Path $root 'dist\kernmem-cs2.exe') -WorkingDirectory (Join-Path $root 'dist') | Out-Null

for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Seconds 1

    $proc = Get-Process kernmem-cs2 -ErrorAction SilentlyContinue
    $win  = 0
    if ($proc) { $win = $proc.MainWindowHandle }

    $name = ''
    if (Test-Path $state) {
        $lines = Get-Content $state
        if ($lines.Count -ge 3 -and $lines[2]) { $name = $lines[2].Trim() }
    }

    $w = ''
    $g = ''
    if ($name) {
        $w = Try-Open "\\.\$name"
        $g = Try-Open "\\?\GLOBALROOT\Device\$name"
    }

    "[$i s] client=$([bool]$proc) win=$win name=[$name] win32=$w globalroot=$g"

    if (-not $proc) { break }
}

'=== killed ==='
Get-Process kernmem-cs2 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch { } }
Get-Process kernmem-loader -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch { } }
