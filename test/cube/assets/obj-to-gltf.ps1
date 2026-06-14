param(
    [Parameter(Mandatory=$true)][string]$InputObj,
    [Parameter(Mandatory=$true)][string]$Output     # .gltf path; .bin sits beside it
)
# Convert a Wavefront .obj into a minimal glTF 2.0 asset (separate
# .gltf JSON + .bin binary). Supports the subset Renderer3D consumes:
# POSITION + NORMAL + TEXCOORD_0 interleaved, triangulated faces. Runs
# once and the outputs get checked in -- the engine's LoadGltf only
# reads the saved files.

if (-not (Test-Path $InputObj)) { throw "input not found: $InputObj" }

# --- Parse OBJ (re-using the loose convention from the rest of the test
# assets: v, vn, vt, f with 3 or 4 corners, v/vt/vn corner format).
$positions = New-Object System.Collections.Generic.List[single]
$normals   = New-Object System.Collections.Generic.List[single]
$texCoords = New-Object System.Collections.Generic.List[single]
# Each face corner is stored as (vIdx, vtIdx, vnIdx). 0-based.
$corners   = New-Object System.Collections.Generic.List[object]

function Resolve-Idx([int]$raw, [int]$count) {
    if ($raw -eq 0) { return -1 }
    if ($raw -gt 0) { return $raw - 1 }
    return $count + $raw
}

foreach ($line in Get-Content $InputObj) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    if ($line[0] -eq '#') { continue }
    $parts = $line.Trim() -split '\s+'
    switch ($parts[0]) {
        'v'  {
            $positions.Add([single]$parts[1])
            $positions.Add([single]$parts[2])
            $positions.Add([single]$parts[3])
        }
        'vn' {
            $normals.Add([single]$parts[1])
            $normals.Add([single]$parts[2])
            $normals.Add([single]$parts[3])
        }
        'vt' {
            $texCoords.Add([single]$parts[1])
            # OBJ v is bottom-origin; flip for glTF (top-origin).
            $texCoords.Add([single](1.0 - [single]$parts[2]))
        }
        'f' {
            $face = @()
            for ($i = 1; $i -lt $parts.Length; $i++) {
                $tokens = $parts[$i] -split '/'
                $vIdx  = Resolve-Idx ([int]$tokens[0])  ($positions.Count / 3)
                $vtIdx = -1
                $vnIdx = -1
                if ($tokens.Length -ge 2 -and $tokens[1].Length -gt 0) {
                    $vtIdx = Resolve-Idx ([int]$tokens[1]) ($texCoords.Count / 2)
                }
                if ($tokens.Length -ge 3 -and $tokens[2].Length -gt 0) {
                    $vnIdx = Resolve-Idx ([int]$tokens[2]) ($normals.Count / 3)
                }
                $face += ,@($vIdx, $vtIdx, $vnIdx)
            }
            # Fan-triangulate quads + n-gons.
            for ($i = 1; $i -lt $face.Length - 1; $i++) {
                $corners.Add($face[0])
                $corners.Add($face[$i])
                $corners.Add($face[$i + 1])
            }
        }
    }
}

# --- Dedup (v, vt, vn) triples like LoadObj does, so glTF accessors
# can stay interleaved without exploding the vertex count.
$dedup = @{}
$gltfPos = New-Object System.Collections.Generic.List[single]
$gltfNrm = New-Object System.Collections.Generic.List[single]
$gltfUV  = New-Object System.Collections.Generic.List[single]
$gltfIdx = New-Object System.Collections.Generic.List[uint32]
$hasNrm  = $normals.Count -gt 0
$hasUV   = $texCoords.Count -gt 0

foreach ($c in $corners) {
    $key = "$($c[0])/$($c[1])/$($c[2])"
    if ($dedup.ContainsKey($key)) {
        $gltfIdx.Add([uint32]$dedup[$key])
        continue
    }
    $idx = [uint32]$gltfPos.Count / 3
    $dedup[$key] = $idx
    $vi = $c[0]
    $gltfPos.Add([single]$positions[$vi * 3 + 0])
    $gltfPos.Add([single]$positions[$vi * 3 + 1])
    $gltfPos.Add([single]$positions[$vi * 3 + 2])
    if ($hasNrm) {
        if ($c[2] -ge 0) {
            $gltfNrm.Add([single]$normals[$c[2] * 3 + 0])
            $gltfNrm.Add([single]$normals[$c[2] * 3 + 1])
            $gltfNrm.Add([single]$normals[$c[2] * 3 + 2])
        } else {
            $gltfNrm.Add([single]0); $gltfNrm.Add([single]1); $gltfNrm.Add([single]0)
        }
    }
    if ($hasUV) {
        if ($c[1] -ge 0) {
            $gltfUV.Add([single]$texCoords[$c[1] * 2 + 0])
            $gltfUV.Add([single]$texCoords[$c[1] * 2 + 1])
        } else {
            $gltfUV.Add([single]0); $gltfUV.Add([single]0)
        }
    }
    $gltfIdx.Add([uint32]$idx)
}

$vertexCount = [int]($gltfPos.Count / 3)
$indexCount  = $gltfIdx.Count
$useUint16   = $vertexCount -le 65535

# --- Compute per-attribute min/max (glTF requires it on POSITION; we
# emit it on every attribute because it costs nothing and the spec is
# clearer that way).
function MinMax([System.Collections.Generic.List[single]]$arr, [int]$stride) {
    $count = $arr.Count / $stride
    $mins = New-Object 'single[]' $stride
    $maxs = New-Object 'single[]' $stride
    for ($i = 0; $i -lt $stride; $i++) { $mins[$i] = [single]::MaxValue; $maxs[$i] = [single]::MinValue }
    for ($i = 0; $i -lt $count; $i++) {
        for ($j = 0; $j -lt $stride; $j++) {
            $v = $arr[$i * $stride + $j]
            if ($v -lt $mins[$j]) { $mins[$j] = $v }
            if ($v -gt $maxs[$j]) { $maxs[$j] = $v }
        }
    }
    return ,@($mins, $maxs)
}
$posMM = MinMax $gltfPos 3
$nrmMM = if ($hasNrm) { MinMax $gltfNrm 3 } else { $null }
$uvMM  = if ($hasUV ) { MinMax $gltfUV  2 } else { $null }

# --- Pack the .bin: positions, normals, uvs, indices, each tightly
# aligned to 4 bytes (glTF spec).
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms

$posOffset = [int]$ms.Position
foreach ($f in $gltfPos) { $bw.Write([single]$f) }
$nrmOffset = -1
if ($hasNrm) {
    $nrmOffset = [int]$ms.Position
    foreach ($f in $gltfNrm) { $bw.Write([single]$f) }
}
$uvOffset = -1
if ($hasUV) {
    $uvOffset = [int]$ms.Position
    foreach ($f in $gltfUV) { $bw.Write([single]$f) }
}
$idxOffset = [int]$ms.Position
if ($useUint16) {
    foreach ($i in $gltfIdx) { $bw.Write([uint16]$i) }
} else {
    foreach ($i in $gltfIdx) { $bw.Write([uint32]$i) }
}
# Pad to 4 bytes.
while (($ms.Position % 4) -ne 0) { $bw.Write([byte]0) }
$binBytes = $ms.ToArray()

# --- Build glTF JSON.
$gltfDir   = Split-Path $Output -Parent
$binName   = [System.IO.Path]::GetFileNameWithoutExtension($Output) + '.bin'
$binPath   = Join-Path $gltfDir $binName

$accessors    = @()
$bufferViews  = @()
$attrs        = @{}
$nextView     = 0
$nextAcc      = 0

function Stride([int]$comp) { return $comp * 4 }
function AddView([int]$offset, [int]$byteLen, [int]$target) {
    $script:bufferViews += @{ buffer = 0; byteOffset = $offset; byteLength = $byteLen; target = $target }
    $idx = $script:nextView; $script:nextView++
    return $idx
}
function AddAccessor([int]$viewIdx, [int]$count, [string]$type, [int]$componentType,
                     [array]$mins, [array]$maxs) {
    $acc = @{ bufferView = $viewIdx; componentType = $componentType; count = $count; type = $type }
    if ($mins) { $acc.min = @(); foreach ($v in $mins) { $acc.min += [single]$v } }
    if ($maxs) { $acc.max = @(); foreach ($v in $maxs) { $acc.max += [single]$v } }
    $script:accessors += $acc
    $idx = $script:nextAcc; $script:nextAcc++
    return $idx
}

# 34962 = ARRAY_BUFFER, 34963 = ELEMENT_ARRAY_BUFFER
$posByteLen = $vertexCount * (Stride 3)
$posView    = AddView $posOffset $posByteLen 34962
$posAcc     = AddAccessor $posView $vertexCount 'VEC3' 5126 $posMM[0] $posMM[1]
$attrs.POSITION = $posAcc

if ($hasNrm) {
    $nrmByteLen = $vertexCount * (Stride 3)
    $nrmView    = AddView $nrmOffset $nrmByteLen 34962
    $nrmAcc     = AddAccessor $nrmView $vertexCount 'VEC3' 5126 $nrmMM[0] $nrmMM[1]
    $attrs.NORMAL = $nrmAcc
}
if ($hasUV) {
    $uvByteLen = $vertexCount * (Stride 2)
    $uvView    = AddView $uvOffset $uvByteLen 34962
    $uvAcc     = AddAccessor $uvView $vertexCount 'VEC2' 5126 $uvMM[0] $uvMM[1]
    $attrs.TEXCOORD_0 = $uvAcc
}

if ($useUint16) {
    $idxByteLen = $indexCount * 2
    $idxComp    = 5123
} else {
    $idxByteLen = $indexCount * 4
    $idxComp    = 5125
}
$idxView = AddView $idxOffset $idxByteLen 34963
$idxAcc  = AddAccessor $idxView $indexCount 'SCALAR' $idxComp $null $null

$gltf = @{
    asset       = @{ version = '2.0'; generator = 'NaraGame obj-to-gltf.ps1' }
    buffers     = @( @{ uri = $binName; byteLength = $binBytes.Length } )
    bufferViews = $bufferViews
    accessors   = $accessors
    meshes      = @( @{ primitives = @( @{ attributes = $attrs; indices = $idxAcc; mode = 4 } ) } )  # mode 4 = TRIANGLES
    nodes       = @( @{ mesh = 0 } )
    scenes      = @( @{ nodes = @(0) } )
    scene       = 0
}

$json = $gltf | ConvertTo-Json -Depth 12

$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($Output, $json, $utf8)
[System.IO.File]::WriteAllBytes($binPath, $binBytes)
"wrote $Output ($vertexCount verts, $indexCount indices)"
"wrote $binPath ($($binBytes.Length) bytes)"
