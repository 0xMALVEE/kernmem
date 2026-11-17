$sig = @'
[DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
[DllImport("kernel32", SetLastError=true)]
public static extern bool CloseHandle(IntPtr h);
'@

$k32 = Add-Type -MemberDefinition $sig -Name K32e -Namespace W5 -PassThru
$read  = [uint32]2147483648
$share = [uint32]3
$open  = [uint32]3

function Try-Open([string]$path) {
    $h = $k32::CreateFileW($path, $read, $share, [IntPtr]::Zero, $open, [uint32]0, [IntPtr]::Zero)

    if ($h.ToInt64() -eq -1) {
        $c = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        return "FAIL($c)"
    }

    [void]$k32::CloseHandle($h)
    return 'OK'
}

$root  = $PSScriptRoot
$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'

Get-Process kernmem-loader -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch { } }
Remove-Item $state -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

Start-Process -FilePath (Join-Path $root 'dist\kernmem-loader.exe') `
    -WorkingDirectory (Join-Path $root 'dist') -Verb RunAs -ArgumentList 'load' | Out-Null

$seen = $false

for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 150

    if (-not (Test-Path $state)) { continue }

    $lines = Get-Content $state
    if ($lines.Count -lt 3 -or -not $lines[2]) { continue }

    $name = $lines[2].Trim()
    $seen = $true

    "[$($i * 150)ms] name=$name win32=$(Try-Open "\\.\$name") globalroot=$(Try-Open "\\?\GLOBALROOT\Device\$name")"
}

if (-not $seen) { 'state file never gained a device name' }

Start-Sleep -Seconds 1

'--- after loader exit ---'
if (Test-Path $state) {
    $lines = Get-Content $state
    if ($lines.Count -ge 3 -and $lines[2]) {
        $name = $lines[2].Trim()
        "name=$name win32=$(Try-Open "\\.\$name") globalroot=$(Try-Open "\\?\GLOBALROOT\Device\$name")"
    }
}

'--- loader alive? ---'
(Get-Process kernmem-loader -ErrorAction SilentlyContinue | Measure-Object).Count
