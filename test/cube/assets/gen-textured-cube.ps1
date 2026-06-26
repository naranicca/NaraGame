# Generate a textured unit cube glTF asset for the materials demo.
#
# Outputs:
#   textured-cube.gltf  -- JSON descriptor pointing into .bin and at an
#                          embedded PNG bufferView
#   textured-cube.bin   -- vertex / index / image bytes packed back-to-back
#
# The cube is 24 verts (4 per face × 6 faces so each face has its own
# normal and UVs), 36 indices, and a 16×16 PNG laid out as four
# colour quadrants (red / green / blue / yellow) so the cube's UVs land
# on visually distinct colours per face.
#
# pack-glb.ps1 on the .gltf output produces textured-cube.glb where the
# image is already a bufferView -- the glTF loader's embedded-image path
# picks it straight out of the .glb's BIN chunk.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$assets = Split-Path -Parent $MyInvocation.MyCommand.Path
$binPath  = Join-Path $assets 'textured-cube.bin'
$gltfPath = Join-Path $assets 'textured-cube.gltf'

# ---------------------------------------------------------------------------
# Cube geometry. Six faces, each as a quad with face-aligned normal.
# Vertex layout: 3 floats position, 3 floats normal, 2 floats UV.
# Faces wind CCW seen from outside the cube (matches glTF convention).
$faces = @(
    # +X face (normal +1 0 0)
    @{ N=@(1, 0, 0);  P=@(@(0.5,-0.5, 0.5), @(0.5,-0.5,-0.5), @(0.5, 0.5,-0.5), @(0.5, 0.5, 0.5)) },
    # -X face
    @{ N=@(-1, 0, 0); P=@(@(-0.5,-0.5,-0.5), @(-0.5,-0.5, 0.5), @(-0.5, 0.5, 0.5), @(-0.5, 0.5,-0.5)) },
    # +Y face
    @{ N=@(0, 1, 0);  P=@(@(-0.5, 0.5, 0.5), @( 0.5, 0.5, 0.5), @( 0.5, 0.5,-0.5), @(-0.5, 0.5,-0.5)) },
    # -Y face
    @{ N=@(0,-1, 0);  P=@(@(-0.5,-0.5,-0.5), @( 0.5,-0.5,-0.5), @( 0.5,-0.5, 0.5), @(-0.5,-0.5, 0.5)) },
    # +Z face
    @{ N=@(0, 0, 1);  P=@(@(-0.5,-0.5, 0.5), @( 0.5,-0.5, 0.5), @( 0.5, 0.5, 0.5), @(-0.5, 0.5, 0.5)) },
    # -Z face
    @{ N=@(0, 0,-1);  P=@(@( 0.5,-0.5,-0.5), @(-0.5,-0.5,-0.5), @(-0.5, 0.5,-0.5), @( 0.5, 0.5,-0.5)) }
)

# Each face uses the full (0..1) UV square. The texture's four quadrants
# pick up across faces so adjacent faces show different colour blocks.
$faceUVs = @(@(0,1), @(1,1), @(1,0), @(0,0))

# Non-interleaved layout: one stream per attribute (positions packed,
# then normals packed, then UVs packed). The engine's accessor reader
# expects tightly-packed buffers per accessor -- it doesn't honour
# byteStride yet -- so interleaved would render as a tangle of spikes.
$posFloats = New-Object 'System.Collections.Generic.List[single]'
$nrmFloats = New-Object 'System.Collections.Generic.List[single]'
$uvFloats  = New-Object 'System.Collections.Generic.List[single]'
$indexShorts = New-Object 'System.Collections.Generic.List[uint16]'

$base = 0
foreach ($f in $faces) {
    foreach ($pi in 0..3) {
        $p = $f.P[$pi]
        $u = $faceUVs[$pi]
        $posFloats.Add([single]$p[0]); $posFloats.Add([single]$p[1]); $posFloats.Add([single]$p[2])
        $nrmFloats.Add([single]$f.N[0]); $nrmFloats.Add([single]$f.N[1]); $nrmFloats.Add([single]$f.N[2])
        $uvFloats.Add([single]$u[0]);  $uvFloats.Add([single]$u[1])
    }
    # Two CCW triangles: (0,1,2) (0,2,3)
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
$idxBytes = New-Object byte[] ($indexShorts.Count * 2)
for ($i = 0; $i -lt $indexShorts.Count; $i++) {
    $b = [System.BitConverter]::GetBytes([uint16]$indexShorts[$i])
    [Array]::Copy($b, 0, $idxBytes, $i * 2, 2)
}

# ---------------------------------------------------------------------------
# Texture: 16×16 PNG with four colour quadrants.
$bmp = New-Object System.Drawing.Bitmap 16, 16
try {
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $colors = @(
            [System.Drawing.Color]::FromArgb(255, 230,  60,  60), # red
            [System.Drawing.Color]::FromArgb(255,  60, 200,  80), # green
            [System.Drawing.Color]::FromArgb(255,  60,  90, 230), # blue
            [System.Drawing.Color]::FromArgb(255, 240, 210,  50)  # yellow
        )
        $brushes = @(
            (New-Object System.Drawing.SolidBrush $colors[0]),
            (New-Object System.Drawing.SolidBrush $colors[1]),
            (New-Object System.Drawing.SolidBrush $colors[2]),
            (New-Object System.Drawing.SolidBrush $colors[3])
        )
        try {
            $g.FillRectangle($brushes[0], 0, 0, 8, 8)
            $g.FillRectangle($brushes[1], 8, 0, 8, 8)
            $g.FillRectangle($brushes[2], 0, 8, 8, 8)
            $g.FillRectangle($brushes[3], 8, 8, 8, 8)
        } finally {
            foreach ($br in $brushes) { $br.Dispose() }
        }
    } finally {
        $g.Dispose()
    }
    $ms = New-Object System.IO.MemoryStream
    try {
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $pngBytes = $ms.ToArray()
    } finally {
        $ms.Dispose()
    }
} finally {
    $bmp.Dispose()
}

# ---------------------------------------------------------------------------
# Layout inside .bin: POS | NORMAL | UV | indices | PNG, each padded to 4B.
function PadTo4($n) { return [int][math]::Ceiling($n / 4.0) * 4 }
$posOff = 0
$posLen = $posBytes.Length
$nrmOff = PadTo4 ($posOff + $posLen)
$nrmLen = $nrmBytes.Length
$uvOff  = PadTo4 ($nrmOff + $nrmLen)
$uvLen  = $uvBytes.Length
$idxOff = PadTo4 ($uvOff + $uvLen)
$idxLen = $idxBytes.Length
$pngOff = PadTo4 ($idxOff + $idxLen)
$pngLen = $pngBytes.Length
$total  = $pngOff + $pngLen

$binAll = New-Object byte[] $total
[Array]::Copy($posBytes, 0, $binAll, $posOff, $posLen)
[Array]::Copy($nrmBytes, 0, $binAll, $nrmOff, $nrmLen)
[Array]::Copy($uvBytes,  0, $binAll, $uvOff,  $uvLen)
[Array]::Copy($idxBytes, 0, $binAll, $idxOff, $idxLen)
[Array]::Copy($pngBytes, 0, $binAll, $pngOff, $pngLen)

[System.IO.File]::WriteAllBytes($binPath, $binAll)

# ---------------------------------------------------------------------------
# glTF JSON. Hand-built so we control bufferView byteOffsets / byteLengths.
$json = @"
{
  "asset": { "version": "2.0", "generator": "NaraGame gen-textured-cube.ps1" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes":  [{ "mesh": 0 }],
  "meshes": [{
    "primitives": [{
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "indices": 3,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "ColourBlocks",
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
      "baseColorTexture": { "index": 0 },
      "metallicFactor": 0.0,
      "roughnessFactor": 0.7
    }
  }],
  "textures": [{ "source": 0 }],
  "images":   [{ "bufferView": 4, "mimeType": "image/png" }],
  "buffers": [{
    "uri": "textured-cube.bin",
    "byteLength": $total
  }],
  "bufferViews": [
    { "buffer": 0, "byteOffset": $posOff, "byteLength": $posLen },
    { "buffer": 0, "byteOffset": $nrmOff, "byteLength": $nrmLen },
    { "buffer": 0, "byteOffset": $uvOff,  "byteLength": $uvLen  },
    { "buffer": 0, "byteOffset": $idxOff, "byteLength": $idxLen },
    { "buffer": 0, "byteOffset": $pngOff, "byteLength": $pngLen }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 24, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0, "componentType": 5126, "count": 24, "type": "VEC3" },
    { "bufferView": 2, "byteOffset": 0, "componentType": 5126, "count": 24, "type": "VEC2" },
    { "bufferView": 3, "byteOffset": 0, "componentType": 5123, "count": 36, "type": "SCALAR" }
  ]
}
"@
[System.IO.File]::WriteAllText($gltfPath, $json,
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host ("gen-textured-cube: wrote {0} ({1} bytes) and {2}" -f `
    (Split-Path -Leaf $binPath), $total, (Split-Path -Leaf $gltfPath))
Write-Host ("  pos {0}..{1}, nrm {2}..{3}, uv {4}..{5}, idx {6}..{7}, png {8}..{9}" -f `
    $posOff, ($posOff + $posLen),
    $nrmOff, ($nrmOff + $nrmLen),
    $uvOff,  ($uvOff  + $uvLen),
    $idxOff, ($idxOff + $idxLen),
    $pngOff, ($pngOff + $pngLen))
