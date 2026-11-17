$sig = @'
[DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
[DllImport("kernel32", SetLastError=true)]
public static extern bool CloseHandle(IntPtr h);
'@

$k32 = Add-Type -MemberDefinition $sig -Name K32d -Namespace W4 -PassThru

$read = [uint32]2147483648
$share = [uint32]3
$open = [uint32]3

function Try-Open([string]$path) {
    $h = $k32::CreateFileW($path, $read, $share, [IntPtr]::Zero, $open, [uint32]0, [IntPtr]::Zero)

    if ($h.ToInt64() -eq -1) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        "FAIL ($code): $path"
    } else {
        "OK: $path"
        [void]$k32::CloseHandle($h)
    }
}

$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'
$lines = Get-Content $state
$name = $lines[2].Trim()
"name: [$name]  length: $($name.Length)"

Try-Open "\\.\$name"
Try-Open "\??\$name"
Try-Open "\\?\GLOBALROOT\Device\$name"

'--- dos device dir entries ---'
try {
    $d = [IO.Directory]::GetFiles('\\.\')
    "dir readable, count $($d.Count)"
}
catch { "dir unreadable: $($_.Exception.Message)" }
