# Generate a 256x128 equirectangular sunset.hdr (Radiance RGBE).
#
# The sky is a vertical gradient -- deep blue zenith through warm orange
# horizon to dark earth below -- with a bright HDR sun spot placed in
# the direction of the cube test scene's directional light, so the IBL
# reflection on metal spheres shows a recognisable sun highlight after
# Renderer3D::LoadEnvironment loads this file.
#
# Output is RLE-encoded so the decoder's RLE path gets exercised. The
# Radiance RLE format wraps each scanline in (2, 2, w_hi, w_lo) and
# then encodes each of the four RGBE channels independently with raw or
# run-length runs of <= 128 bytes.

$ErrorActionPreference = 'Stop'

$assets = Split-Path -Parent $MyInvocation.MyCommand.Path
$outPath = Join-Path $assets 'sunset.hdr'

$w = 256
$h = 128

# Sun position in the texture, matching the cube scene's default
# light.direction (-0.4, -1.0, -0.3). The sun is along -direction; we
# map that to (u, v) via the same atan2/acos the shader uses so the
# reflection on metal spheres lines up.
$dirX = 0.4; $dirY = 1.0; $dirZ = 0.3
$dlen = [math]::Sqrt($dirX * $dirX + $dirY * $dirY + $dirZ * $dirZ)
$dx = $dirX / $dlen; $dy = $dirY / $dlen; $dz = $dirZ / $dlen
$uSun = [math]::Atan2($dx, $dz) / (2 * [math]::PI) + 0.5
$vSun = [math]::Acos($dy) / [math]::PI

function SkyColor([float] $u, [float] $v) {
    # Body gradient.
    if ($v -lt 0.45) {
        $t = $v / 0.45
        # Deep blue zenith -> warm orange just above horizon.
        $r = 0.05 * (1 - $t) + 3.0 * $t
        $g = 0.10 * (1 - $t) + 1.5 * $t
        $b = 0.30 * (1 - $t) + 0.6 * $t
    } elseif ($v -lt 0.55) {
        # Bright horizon band.
        $r = 5.0; $g = 2.5; $b = 0.8
    } else {
        $t = ($v - 0.55) / 0.45
        $r = 0.50 * (1 - $t) + 0.05 * $t
        $g = 0.30 * (1 - $t) + 0.03 * $t
        $b = 0.15 * (1 - $t) + 0.02 * $t
    }

    # Sun spot. Gaussian falloff in (u, v) with extra punch in the
    # peak so the prefiltered env still reads a sun even after GGX
    # blurring at the rougher cascades.
    $du = $u - $uSun
    if ($du -gt  0.5) { $du = $du - 1.0 }
    if ($du -lt -0.5) { $du = $du + 1.0 }
    $dv = $v - $vSun
    $d2 = $du * $du + $dv * $dv
    $sun = 60.0 * [math]::Exp(-$d2 / (2.0 * 0.015 * 0.015))
    $r = $r + $sun
    $g = $g + $sun * 0.65
    $b = $b + $sun * 0.30

    return @($r, $g, $b)
}

# Convert a single (r, g, b) float triplet to RGBE.
function ToRgbe([float] $r, [float] $g, [float] $b) {
    $mx = [math]::Max($r, [math]::Max($g, $b))
    if ($mx -lt 1e-32) { return @(0, 0, 0, 0) }
    # frexp-style decomposition: max = m * 2^e with m in [0.5, 1).
    $e = 0; $m = $mx
    while ($m -ge 1.0)              { $m = $m / 2.0; $e = $e + 1 }
    while ($m -lt 0.5 -and $m -gt 0) { $m = $m * 2.0; $e = $e - 1 }
    $scale = $m * 256.0 / $mx
    $R = [int][math]::Floor($r * $scale); if ($R -gt 255) { $R = 255 }; if ($R -lt 0) { $R = 0 }
    $G = [int][math]::Floor($g * $scale); if ($G -gt 255) { $G = 255 }; if ($G -lt 0) { $G = 0 }
    $B = [int][math]::Floor($b * $scale); if ($B -gt 255) { $B = 255 }; if ($B -lt 0) { $B = 0 }
    $E = [int]($e + 128)
    if ($E -gt 255) { $E = 255 }; if ($E -lt 0) { $E = 0 }
    return @($R, $G, $B, $E)
}

$bytes = New-Object 'System.Collections.Generic.List[byte]'

# ASCII header.
$header = "#?RADIANCE`nFORMAT=32-bit_rle_rgbe`n`n-Y $h +X $w`n"
foreach ($c in $header.ToCharArray()) { [void]$bytes.Add([byte] $c) }

# Each scanline holds W pixels; encode the four channels (R, G, B, E)
# independently as a sequence of raw runs of <= 128 bytes.
function AppendRawRun($bytes, $channelBytes, $start, $count) {
    [void]$bytes.Add([byte] $count)
    for ($k = 0; $k -lt $count; $k++) {
        [void]$bytes.Add([byte] $channelBytes[$start + $k])
    }
}

for ($y = 0; $y -lt $h; $y++) {
    $v = $y / ($h - 1)
    # Build the four channel arrays for this scanline.
    $rCh = New-Object 'byte[]' $w
    $gCh = New-Object 'byte[]' $w
    $bCh = New-Object 'byte[]' $w
    $eCh = New-Object 'byte[]' $w
    for ($x = 0; $x -lt $w; $x++) {
        $u = $x / ($w - 1)
        $col = SkyColor $u $v
        $rgbe = ToRgbe $col[0] $col[1] $col[2]
        $rCh[$x] = [byte]$rgbe[0]
        $gCh[$x] = [byte]$rgbe[1]
        $bCh[$x] = [byte]$rgbe[2]
        $eCh[$x] = [byte]$rgbe[3]
    }

    # Scanline header: 2, 2, hi, lo of width.
    [void]$bytes.Add([byte] 2)
    [void]$bytes.Add([byte] 2)
    [void]$bytes.Add([byte] (($w -shr 8) -band 0x7F))
    [void]$bytes.Add([byte] ($w -band 0xFF))

    foreach ($ch in @($rCh, $gCh, $bCh, $eCh)) {
        $pos = 0
        while ($pos -lt $w) {
            $left = $w - $pos
            $take = if ($left -gt 128) { 128 } else { $left }
            AppendRawRun $bytes $ch $pos $take
            $pos += $take
        }
    }
}

[System.IO.File]::WriteAllBytes($outPath, $bytes.ToArray())
Write-Host ("gen-sunset-hdr: wrote {0} ({1} bytes; sun at u={2:F3} v={3:F3})" -f `
    (Split-Path -Leaf $outPath), $bytes.Count, $uSun, $vSun)
