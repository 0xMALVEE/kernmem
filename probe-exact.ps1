$sig = @'
[DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
[DllImport("kernel32", SetLastError=true)]
public static extern bool CloseHandle(IntPtr h);
'@

$k32 = Add-Type -MemberDefinition $sig -Name K32g -Namespace W7 -PassThru

$access = [uint32]3221225472   # GENERIC_READ|GENERIC_WRITE
$share  = [uint32]3
$open   = [uint32]3

function Try-Exact([string]$path, [uint32]$flags) {
    $h = $k32::CreateFileW($path, $access, $share, [IntPtr]::Zero, $open, $flags, [IntPtr]::Zero)

    if ($h.ToInt64() -eq -1) {
        return "FAIL($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
    }

    [void]$k32::CloseHandle($h)
    return 'OK'
}

$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'
$lines = Get-Content $state
$name  = $lines[2].Trim()

"name: $name"
"win32  flags=0    : $(Try-Exact "\\.\$name" ([uint32]0))"
"win32  flags=NORM : $(Try-Exact "\\.\$name" ([uint32]128))"
"global flags=0    : $(Try-Exact "\\?\GLOBALROOT\Device\$name" ([uint32]0))"
"global flags=NORM : $(Try-Exact "\\?\GLOBALROOT\Device\$name" ([uint32]128))"
