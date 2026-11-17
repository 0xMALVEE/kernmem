$sig = @'
[DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
[DllImport("kernel32", SetLastError=true)]
public static extern bool CloseHandle(IntPtr h);
'@

$k32 = Add-Type -MemberDefinition $sig -Name K32h -Namespace W8 -PassThru
$access = [uint32]3221225472
$share  = [uint32]3
$open   = [uint32]3
$flags  = [uint32]128

function Try-Path([string]$path) {
    $h = $k32::CreateFileW($path, $access, $share, [IntPtr]::Zero, $open, $flags, [IntPtr]::Zero)

    if ($h.ToInt64() -eq -1) {
        return "FAIL($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
    }

    [void]$k32::CloseHandle($h)
    return 'OK'
}

$state = Join-Path ([IO.Path]::GetTempPath()) 'km-nal.state'
$bytes = [IO.File]::ReadAllBytes($state)

'--- bytes ---'
($bytes | ForEach-Object { $_.ToString('X2') }) -join ' '

$raw = [Text.Encoding]::ASCII.GetString($bytes)
$lines = $raw -split "`n"
"split count: $($lines.Count)"

$third = $lines[2]
"third raw  : [$third]  len=$($third.Length)"

$trim = $third.TrimEnd()
"third trim : [$trim]  len=$($trim.Length)"

'--- probes (trimmed) ---'
"global: $(Try-Path "\\?\GLOBALROOT\Device\$trim")"
"win32 : $(Try-Path "\\.\$trim")"

'--- probes (raw, newline kept) ---'
"global: $(Try-Path "\\?\GLOBALROOT\Device\$third")"
"win32 : $(Try-Path "\\.\$third")"
