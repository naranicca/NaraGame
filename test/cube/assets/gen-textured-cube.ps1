# Generate a textured unit cube glTF asset for the materials demo.
#
# Outputs:
#   textured-cube.gltf  -- JSON descriptor pointing into .bin and at the
#                          embedded baseColor + normal-map PNGs
#   textured-cube.bin   -- POSITION | NORMAL | UV | TANGENT | indices |
#                          baseColor.png | normal.png (each padded to 4B)
#
# The cube is 24 verts (4 per face × 6 faces so each face has its own
# normal, UVs, and tangent) and 36 indices. Each face's UVs span the
# full (0..1) square so all four quadrants of every texture sample
# across every face.
#
# Per-face TANGENTs are precomputed by hand using glTF's convention
# (xyz = +U direction in tangent space, w = bitangent sign such that
# bitangent = cross(N, T.xyz) * T.w lines up with the +V direction).
#
# pack-glb.ps1 on the .gltf output produces textured-cube.glb where the
# images are already bufferViews -- the glTF loader picks them straight
# out of the .glb's BIN chunk.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$assets = Split-Path -Parent $MyInvocation.MyCommand.Path
$binPath  = Join-Path $assets 'textured-cube.bin'
$gltfPath = Join-Path $assets 'textured-cube.gltf'

# ---------------------------------------------------------------------------
# Cube geometry. Six faces, each as a quad with face-aligned normal +
# the tangent that maps +U into world space for that face. Wind CCW seen
# from outside the cube.
$faces = @(
    # +X face
    @{ N=@(1, 0, 0);  T=@(0, 0,-1,-1);
       P=@(@(0.5,-0.5, 0.5), @(0.5,-0.5,-0.5), @(0.5, 0.5,-0.5), @(0.5, 0.5, 0.5)) },
    # -X face
    @{ N=@(-1, 0, 0); T=@(0, 0, 1, 1);
       P=@(@(-0.5,-0.5,-0.5), @(-0.5,-0.5, 0.5), @(-0.5, 0.5, 0.5), @(-0.5, 0.5,-0.5)) },
    # +Y face
    @{ N=@(0, 1, 0);  T=@(1, 0, 0,-1);
       P=@(@(-0.5, 0.5, 0.5), @( 0.5, 0.5, 0.5), @( 0.5, 0.5,-0.5), @(-0.5, 0.5,-0.5)) },
    # -Y face
    @{ N=@(0,-1, 0);  T=@(1, 0, 0,-1);
       P=@(@(-0.5,-0.5,-0.5), @( 0.5,-0.5,-0.5), @( 0.5,-0.5, 0.5), @(-0.5,-0.5, 0.5)) },
    # +Z face
    @{ N=@(0, 0, 1);  T=@(1, 0, 0,-1);
       P=@(@(-0.5,-0.5, 0.5), @( 0.5,-0.5, 0.5), @( 0.5, 0.5, 0.5), @(-0.5, 0.5, 0.5)) },
    # -Z face
    @{ N=@(0, 0,-1); T=@(-1, 0, 0,-1);
       P=@(@( 0.5,-0.5,-0.5), @(-0.5,-0.5,-0.5), @(-0.5, 0.5,-0.5), @( 0.5, 0.5,-0.5)) }
)

$faceUVs = @(@(0,1), @(1,1), @(1,0), @(0,0))

# Non-interleaved layout: one stream per attribute. The engine's accessor
# reader expects tightly-packed buffers per accessor -- it doesn't honour
# byteStride yet, so interleaved would render as a tangle of spikes.
$posFloats = New-Object 'System.Collections.Generic.List[single]'
$nrmFloats = New-Object 'System.Collections.Generic.List[single]'
$uvFloats  = New-Object 'System.Collections.Generic.List[single]'
$tanFloats = New-Object 'System.Collections.Generic.List[single]'
$indexShorts = New-Object 'System.Collections.Generic.List[uint16]'

$base = 0
foreach ($f in $faces) {
    foreach ($pi in 0..3) {
        $p = $f.P[$pi]
        $u = $faceUVs[$pi]
        $posFloats.Add([single]$p[0]); $posFloats.Add([single]$p[1]); $posFloats.Add([single]$p[2])
        $nrmFloats.Add([single]$f.N[0]); $nrmFloats.Add([single]$f.N[1]); $nrmFloats.Add([single]$f.N[2])
        $uvFloats.Add([single]$u[0]);  $uvFloats.Add([single]$u[1])
        $tanFloats.Add([single]$f.T[0]); $tanFloats.Add([single]$f.T[1])
        $tanFloats.Add([single]$f.T[2]); $tanFloats.Add([single]$f.T[3])
    }
    $indexShorts.Add([uint16]($base + 0))
    $indexShorts.Add([uint16]($base + 1))
    $indexShorts.Add([uint16]($base + 2))
    $indexShorts.Add([uint16]($base + 0))
    $indexShorts.Add([uint16]($base + 2))
    $indexShorts.Add([uint16]($base + 3))
    $base += 4
}

function FloatsToBytes($list) {
    $out = New-Object byte[] ($list.Count * 4)
    for ($i = 0; $i -lt $list.Count; $i++) {
        $b = [System.BitConverter]::GetBytes([single]$list[$i])
        [Array]::Copy($b, 0, $out, $i * 4, 4)
    }
    return $out
}

$posBytes = FloatsToBytes $posFloats
$nrmBytes = FloatsToBytes $nrmFloats
$uvBytes  = FloatsToBytes $uvFloats
$tanBytes = FloatsToBytes $tanFloats
$idxBytes = New-Object byte[] ($indexShorts.Count * 2)
for ($i = 0; $i -lt $indexShorts.Count; $i++) {
    $b = [System.BitConverter]::GetBytes([uint16]$indexShorts[$i])
    [Array]::Copy($b, 0, $idxBytes, $i * 2, 2)
}

# ---------------------------------------------------------------------------
# baseColor: 16×16 PNG with four colour quadrants.
function MakeBaseColorPng() {
    $bmp = New-Object System.Drawing.Bitmap 16, 16
    try {
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        try {
            $cols = @(
                [System.Drawing.Color]::FromArgb(255, 230,  60,  60), # red
                [System.Drawing.Color]::FromArgb(255,  60, 200,  80), # green
                [System.Drawing.Color]::FromArgb(255,  60,  90, 230), # blue
                [System.Drawing.Color]::FromArgb(255, 240, 210,  50)  # yellow
            )
            $brushes = @()
            foreach ($c in $cols) { $brushes += (New-Object System.Drawing.SolidBrush $c) }
            try {
                $g.FillRectangle($brushes[0], 0, 0, 8, 8)
                $g.FillRectangle($brushes[1], 8, 0, 8, 8)
                $g.FillRectangle($brushes[2], 0, 8, 8, 8)
                $g.FillRectangle($brushes[3], 8, 8, 8, 8)
            } finally {
                foreach ($br in $brushes) { $br.Dispose() }
            }
        } finally { $g.Dispose() }
        $ms = New-Object System.IO.MemoryStream
        try {
            $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
            return $ms.ToArray()
        } finally { $ms.Dispose() }
    } finally { $bmp.Dispose() }
}

# normalMap: 16x16 PNG encoding a smooth sinusoidal "bumpy" surface. The
# perturbation amplitude (0.4) is large enough to be obvious under a
# single directional light but small enough that the sphere-grid lighting
# rest of the scene still reads as flat.
function MakeNormalPng() {
    $bmp = New-Object System.Drawing.Bitmap 16, 16
    try {
        for ($y = 0; $y -lt 16; $y++) {
            for ($x = 0; $x -lt 16; $x++) {
                # Period 16 (one full wave across the face) at amplitude
                # 0.7 makes the bumps obvious even on a small cube.
                $tx = [math]::Sin(2 * [math]::PI * $x / 16.0) * 0.7
                $ty = [math]::Sin(2 * [math]::PI * $y / 16.0) * 0.7
                $tzs = 1.0 - $tx*$tx - $ty*$ty
                if ($tzs -lt 0) { $tzs = 0 }
                $tz = [math]::Sqrt($tzs)
                $r = [byte]([math]::Round(($tx * 0.5 + 0.5) * 255))
                $g = [byte]([math]::Round(($ty * 0.5 + 0.5) * 255))
                $b = [byte]([math]::Round(($tz * 0.5 + 0.5) * 255))
                $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, $r, $g, $b))
            }
        }
        $ms = New-Object System.IO.MemoryStream
        try {
            $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
            return $ms.ToArray()
        } finally { $ms.Dispose() }
    } finally { $bmp.Dispose() }
}

$colorPngBytes  = MakeBaseColorPng
$normalPngBytes = MakeNormalPng

# ---------------------------------------------------------------------------
# .bin layout: POS | NORMAL | UV | TANGENT | indices | colorPNG | normalPNG,
# each padded to 4B so accessor byteOffsets stay aligned.
function PadTo4($n) { return [int][math]::Ceiling($n / 4.0) * 4 }

$posOff = 0
$posLen = $posBytes.Length
$nrmOff = PadTo4 ($posOff + $posLen);  $nrmLen = $nrmBytes.Length
$uvOff  = PadTo4 ($nrmOff + $nrmLen);  $uvLen  = $uvBytes.Length
$tanOff = PadTo4 ($uvOff  + $uvLen);   $tanLen = $tanBytes.Length
$idxOff = PadTo4 ($tanOff + $tanLen);  $idxLen = $idxBytes.Length
$colOff = PadTo4 ($idxOff + $idxLen);  $colLen = $colorPngBytes.Length
$nmpOff = PadTo4 ($colOff + $colLen);  $nmpLen = $normalPngBytes.Length
$total  = $nmpOff + $nmpLen

$binAll = New-Object byte[] $total
[Array]::Copy($posBytes,       0, $binAll, $posOff, $posLen)
[Array]::Copy($nrmBytes,       0, $binAll, $nrmOff, $nrmLen)
[Array]::Copy($uvBytes,        0, $binAll, $uvOff,  $uvLen)
[Array]::Copy($tanBytes,       0, $binAll, $tanOff, $tanLen)
[Array]::Copy($idxBytes,       0, $binAll, $idxOff, $idxLen)
[Array]::Copy($colorPngBytes,  0, $binAll, $colOff, $colLen)
[Array]::Copy($normalPngBytes, 0, $binAll, $nmpOff, $nmpLen)

[System.IO.File]::WriteAllBytes($binPath, $binAll)

# ---------------------------------------------------------------------------
# glTF JSON. TANGENT accessor is the new piece; materials gain a normal-
# Texture entry pointing at images[1].
$json = @"
{
  "asset": { "version": "2.0", "generator": "NaraGame gen-textured-cube.ps1" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes":  [{ "mesh": 0 }],
  "meshes": [{
    "primitives": [{
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TANGENT": 3 },
      "indices": 4,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "ColourBlocksBumpy",
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
      "baseColorTexture": { "index": 0 },
      "metallicFactor": 0.0,
      "roughnessFactor": 0.7
    },
    "normalTexture": { "index": 1 }
  }],
  "textures": [
    { "source": 0 },
    { "source": 1 }
  ],
  "images": [
    { "bufferView": 5, "mimeType": "image/png" },
    { "bufferView": 6, "mimeType": "image/png" }
  ],
  "buffers": [{
    "uri": "textured-cube.bin",
    "byteLength": $total
  }],
  "bufferViews": [
    { "buffer": 0, "byteOffset": $posOff, "byteLength": $posLen },
    { "buffer": 0, "byteOffset": $nrmOff, "byteLength": $nrmLen },
    { "buffer": 0, "byteOffset": $uvOff,  "byteLength": $uvLen  },
    { "buffer": 0, "byteOffset": $tanOff, "byteLength": $tanLen },
    { "buffer": 0, "byteOffset": $idxOff, "byteLength": $idxLen },
    { "buffer": 0, "byteOffset": $colOff, "byteLength": $colLen },
    { "buffer": 0, "byteOffset": $nmpOff, "byteLength": $nmpLen }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 24, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0, "componentType": 5126, "count": 24, "type": "VEC3" },
    { "bufferView": 2, "byteOffset": 0, "componentType": 5126, "count": 24, "type": "VEC2" },
    { "bufferView": 3, "byteOffset": 0, "componentType": 5126, "count": 24, "type": "VEC4" },
    { "bufferView": 4, "byteOffset": 0, "componentType": 5123, "count": 36, "type": "SCALAR" }
  ]
}
"@
[System.IO.File]::WriteAllText($gltfPath, $json,
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host ("gen-textured-cube: wrote {0} ({1} bytes) and {2}" -f `
    (Split-Path -Leaf $binPath), $total, (Split-Path -Leaf $gltfPath))
Write-Host ("  pos {0}..{1}, nrm {2}..{3}, uv {4}..{5}, tan {6}..{7}, idx {8}..{9}" -f `
    $posOff, ($posOff + $posLen), $nrmOff, ($nrmOff + $nrmLen),
    $uvOff,  ($uvOff  + $uvLen),  $tanOff, ($tanOff + $tanLen),
    $idxOff, ($idxOff + $idxLen))
Write-Host ("  baseColorPng {0}..{1}, normalPng {2}..{3}" -f `
    $colOff, ($colOff + $colLen), $nmpOff, ($nmpOff + $nmpLen))
