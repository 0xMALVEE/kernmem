$sig = @'
[DllImport("user32", CharSet=CharSet.Unicode)]
public static extern bool EnumWindows(EnumProc cb, IntPtr param);
public delegate bool EnumProc(IntPtr hWnd, IntPtr param);
[DllImport("user32")]
public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
[DllImport("user32", CharSet=CharSet.Unicode)]
public static extern int GetClassNameW(IntPtr hWnd, System.Text.StringBuilder buf, int max);
[DllImport("user32", CharSet=CharSet.Unicode)]
public static extern int GetWindowTextW(IntPtr hWnd, System.Text.StringBuilder buf, int max);
[DllImport("user32")]
public static extern bool IsWindowVisible(IntPtr hWnd);
[DllImport("user32")]
public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
public struct RECT { public int L, T, R, B; }
'@

$u32 = (Add-Type -MemberDefinition $sig -Name U32 -Namespace WP -PassThru)[0]

$target = (Get-Process kernmem-cs2 -ErrorAction SilentlyContinue | Select-Object -First 1).Id

if (-not $target) { 'client not running'; exit 0 }

"client pid: $target"

$found = @()

$cb = [WP.U32+EnumProc]{
    param($hWnd, $param)

    $pid2 = [uint32]0
    [void]$u32::GetWindowThreadProcessId($hWnd, [ref]$pid2)

    if ($pid2 -eq $target) {
        $cls = New-Object System.Text.StringBuilder 256
        $txt = New-Object System.Text.StringBuilder 256
        [void]$u32::GetClassNameW($hWnd, $cls, 256)
        [void]$u32::GetWindowTextW($hWnd, $txt, 256)

        $r = New-Object WP.U32+RECT
        [void]$u32::GetWindowRect($hWnd, [ref]$r)

        $script:found += "hwnd=$hWnd visible=$([bool]$u32::IsWindowVisible($hWnd)) class=[$($cls.ToString())] title=[$($txt.ToString())] rect=$($r.L),$($r.T),$($r.R),$($r.B)"
    }

    return $true
}

[void]$u32::EnumWindows($cb, [IntPtr]::Zero)

if ($found.Count -eq 0) { 'no top-level windows for the client' } else { $found }
