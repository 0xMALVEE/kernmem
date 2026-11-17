Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$sig = @'
[DllImport("user32")]
public static extern int GetSystemMetrics(int index);
'@

$u32 = (Add-Type -MemberDefinition $sig -Name Sm -Namespace SM -PassThru)[0]

$proc = Get-Process kernmem-cs2 -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $proc) { 'client not running'; exit 0 }

"client pid: $($proc.Id)"

$width  = $u32::GetSystemMetrics(0)
$height = $u32::GetSystemMetrics(1)

"screen: $width x $height"

$bmp = New-Object System.Drawing.Bitmap($width, $height)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen(0, 0, 0, 0, $bmp.Size)

$nonBlack = 0
$green = 0
$red = 0
$step = 4

for ($y = 0; $y -lt $height; $y += $step) {
    for ($x = 0; $x -lt $width; $x += $step) {
        $c = $bmp.GetPixel($x, $y)

        if ($c.R -gt 30 -or $c.G -gt 30 -or $c.B -gt 30) { $nonBlack++ }
        if ($c.G -gt 130 -and $c.R -lt 130) { $green++ }
        if ($c.R -gt 150 -and $c.G -lt 130) { $red++ }
    }
}

$gfx.Dispose()
$bmp.Dispose()

$samples = [int](($width / $step) * ($height / $step))

"samples: $samples"
"non-black: $nonBlack"
"green-ish: $green"
"red-ish: $red"
