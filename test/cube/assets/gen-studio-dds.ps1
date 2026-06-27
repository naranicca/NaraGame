# Generate a 128x128 studio.dds cubemap (DX10 + R16G16B16A16_FLOAT).
#
# Each cube face is a single solid HDR colour so reflections on metal
# spheres show a recognisable wall colour:
#   +X = warm red,   -X = cool blue
#   +Y = bright white ceiling, -Y = dark warm floor
#   +Z = saturated green wall, -Z = saturated yellow wall
#
# This exercises the Dds loader's DX10 extension + R16G16B16A16_FLOAT
# code path without needing an external Khronos asset; the same loader
# accepts production .dds files in the same format.

$ErrorActionPreference = 'Stop'

$assets  = Split-Path -Parent $MyInvocation.MyCommand.Path
$outPath = Join-Path $assets 'studio.dds'

$faceSize = 128

# Wall colours in linear HDR. The ceiling is moderately bright so the
# diffuse IBL "lights from above" believably.
$faces = @(
    @{ R = 2.5; G = 0.5; B = 0.4 },  # +X warm red
    @{ R = 0.4; G = 0.6; B = 2.5 },  # -X cool blue
    @{ R = 4.0; G = 4.0; B = 4.0 },  # +Y bright white ceiling
    @{ R = 0.06; G = 0.04; B = 0.03 },  # -Y dark floor
    @{ R = 0.3; G = 2.5; B = 0.4 },  # +Z green
    @{ R = 3.0; G = 2.5; B = 0.4 }   # -Z yellow
)

# IEEE 754 binary32 -> binary16 truncation, matching the engine side.
function FloatToHalfBytes([single] $f) {
    $b = [System.BitConverter]::GetBytes($f)
    $u = [System.BitConverter]::ToUInt32($b, 0)
    $sign = ($u -shr 16) -band 0x8000
    $eRaw = ($u -shr 23) -band 0xFF
    $e = [int]$eRaw - 127 + 15
    $m = $u -band 0x007FFFFF
    $h = 0
    if ($e -ge 31) {
        if ($m -ne 0) { $h = $sign -bor 0x7C00 -bor 0x0200 }
        else          { $h = $sign -bor 0x7C00 }
    } elseif ($e -le 0) {
        $h = $sign
    } else {
        $h = $sign -bor ([uint32]$e -shl 10) -bor ($m -shr 13)
    }
    return [System.BitConverter]::GetBytes([uint16]$h)
}

# Build pixel data: 6 faces, each faceSize^2 pixels, RGBA16F (8 B / pixel).
$bytesPerPixel = 8
$faceBytes     = $faceSize * $faceSize * $bytesPerPixel
$totalBytes    = 6 * $faceBytes
$pixels        = New-Object 'byte[]' $totalBytes
for ($f = 0; $f -lt 6; $f++) {
    $rh = FloatToHalfBytes ([single] $faces[$f].R)
    $gh = FloatToHalfBytes ([single] $faces[$f].G)
    $bh = FloatToHalfBytes ([single] $faces[$f].B)
    $ah = FloatToHalfBytes ([single] 1.0)
    $base = $f * $faceBytes
    for ($i = 0; $i -lt ($faceSize * $faceSize); $i++) {
        $off = $base + $i * $bytesPerPixel
        $pixels[$off + 0] = $rh[0]; $pixels[$off + 1] = $rh[1]
        $pixels[$off + 2] = $gh[0]; $pixels[$off + 3] = $gh[1]
        $pixels[$off + 4] = $bh[0]; $pixels[$off + 5] = $bh[1]
        $pixels[$off + 6] = $ah[0]; $pixels[$off + 7] = $ah[1]
    }
}

# Build DDS header (124 B). Reference: Microsoft DX SDK documentation.
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
try {
    # Magic 'DDS '
    [void]$bw.Write([byte[]](0x44, 0x44, 0x53, 0x20))
    # dwSize
    [void]$bw.Write([uint32] 124)
    # dwFlags: CAPS|HEIGHT|WIDTH|PIXELFORMAT|LINEARSIZE
    [void]$bw.Write([uint32] (0x1 -bor 0x2 -bor 0x4 -bor 0x1000 -bor 0x80000))
    [void]$bw.Write([uint32] $faceSize)             # dwHeight
    [void]$bw.Write([uint32] $faceSize)             # dwWidth
    [void]$bw.Write([uint32] ($faceSize * $bytesPerPixel))  # dwPitchOrLinearSize
    [void]$bw.Write([uint32] 0)                     # dwDepth
    [void]$bw.Write([uint32] 1)                     # dwMipMapCount (base only)
    # dwReserved1[11]
    for ($i = 0; $i -lt 11; $i++) { [void]$bw.Write([uint32] 0) }
    # ddspf (32 B): dwSize=32, dwFlags=FOURCC(0x4), fourCC='DX10', zeroed rest
    [void]$bw.Write([uint32] 32)
    [void]$bw.Write([uint32] 0x4)
    [void]$bw.Write([byte[]](0x44, 0x58, 0x31, 0x30))
    for ($i = 0; $i -lt 5; $i++) { [void]$bw.Write([uint32] 0) }
    # dwCaps = COMPLEX|TEXTURE
    [void]$bw.Write([uint32] (0x8 -bor 0x1000))
    # dwCaps2 = CUBEMAP|ALL_FACES (0x200 | 0xFC00 = 0xFE00)
    [void]$bw.Write([uint32] 0xFE00)
    [void]$bw.Write([uint32] 0)                     # dwCaps3
    [void]$bw.Write([uint32] 0)                     # dwCaps4
    [void]$bw.Write([uint32] 0)                     # dwReserved2

    # DX10 extension header (20 B).
    [void]$bw.Write([uint32] 10)    # DXGI_FORMAT_R16G16B16A16_FLOAT
    [void]$bw.Write([uint32] 3)     # D3D11_RESOURCE_DIMENSION_TEXTURE2D
    [void]$bw.Write([uint32] 0x4)   # miscFlag = TEXTURECUBE
    [void]$bw.Write([uint32] 1)     # arraySize
    [void]$bw.Write([uint32] 0)     # miscFlags2

    # Pixel data: 6 face buffers back-to-back.
    [void]$bw.Write($pixels)
} finally {
    $bw.Dispose()
}

[System.IO.File]::WriteAllBytes($outPath, $ms.ToArray())
$ms.Dispose()

Write-Host ("gen-studio-dds: wrote {0} ({1} bytes; {2}x{2} face, RGBA16F cube)" -f `
    (Split-Path -Leaf $outPath), (Get-Item $outPath).Length, $faceSize)
