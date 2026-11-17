$sig = @'
[DllImport("kernel32", SetLastError=true)]
public static extern bool AttachConsole(uint pid);
[DllImport("kernel32", SetLastError=true)]
public static extern bool FreeConsole();
[DllImport("kernel32", SetLastError=true)]
public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
[DllImport("kernel32", SetLastError=true)]
public static extern bool ReadConsoleOutputCharacterW(IntPtr h, System.Text.StringBuilder buf, uint len, COORD coord, out uint read);
[DllImport("kernel32", SetLastError=true)]
public static extern bool GetConsoleScreenBufferInfo(IntPtr h, out INFO info);
[DllImport("kernel32", SetLastError=true)]
public static extern bool CloseHandle(IntPtr h);
public struct COORD { public short X, Y; }
public struct INFO { public COORD Size, Cursor; public ushort Attr; public SMALLRECT Window; public COORD Max; }
public struct SMALLRECT { public short L, T, R, B; }
'@

$k32 = (Add-Type -MemberDefinition $sig -Name Con -Namespace CN -PassThru)[0]

$proc = Get-Process kernmem-cs2 -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $proc) { 'client not running'; exit 0 }

"client pid: $($proc.Id)"

[void]$k32::FreeConsole()

if (-not $k32::AttachConsole([uint32]$proc.Id)) {
    'could not attach to the client console'
    exit 0
}

$handle = $k32::CreateFileW('CONOUT$', [uint32]3221225472, [uint32]3, [IntPtr]::Zero, [uint32]3, [uint32]0, [IntPtr]::Zero)

if ($handle.ToInt64() -eq -1) {
    'could not open CONOUT$'
    [void]$k32::FreeConsole()
    exit 0
}

$info = New-Object CN.Con+INFO
[void]$k32::GetConsoleScreenBufferInfo($handle, [ref]$info)

$rows = [int]$info.Cursor.Y
$start = [Math]::Max(0, $rows - 22)

'=== console tail ==='

for ($y = $start; $y -le $rows; $y++) {
    $sb = New-Object System.Text.StringBuilder 200
    $coord = New-Object CN.Con+COORD
    $coord.X = 0
    $coord.Y = [short]$y
    $read = [uint32]0

    [void]$k32::ReadConsoleOutputCharacterW($handle, $sb, [uint32]160, $coord, [ref]$read)

    $line = $sb.ToString().TrimEnd()
    if ($line) { $line }
}

[void]$k32::CloseHandle($handle)
[void]$k32::FreeConsole()
