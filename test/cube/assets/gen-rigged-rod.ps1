# Emit a 4-joint rigged cylinder as `rigged-rod.gltf` + `.bin`. The
# geometry is 5 rings * 12 sides = 60 verts; the rig has 4 stacked
# joints along +Y with linear blend weights so each ring binds to one
# joint with a 50/50 cross-blend on the rings between them. An
# embedded animation rotates each joint around +Z with a phase-offset
# sine, matching the procedural rod the cube test used before glTF
# came online so the visual is a known-good reference.
#
# Run once and check the outputs in. The engine's LoadGltfSkinned just
# reads the files; this script is the source of truth.

$outDir   = $PSScriptRoot
$gltfPath = Join-Path $outDir 'rigged-rod.gltf'
$binPath  = Join-Path $outDir 'rigged-rod.bin'

$rings        = 5
$sides        = 12
$jointCount   = 4
$radius       = 0.18
$segmentLen   = 0.40

# --- Geometry ----------------------------------------------------------
$pos = New-Object System.Collections.Generic.List[single]
$nrm = New-Object System.Collections.Generic.List[single]
$uv  = New-Object System.Collections.Generic.List[single]
$ji  = New-Object System.Collections.Generic.List[byte]    # UBYTE4 indices
$jw  = New-Object System.Collections.Generic.List[single]  # FLOAT4 weights
$idx = New-Object System.Collections.Generic.List[uint16]

for ($r = 0; $r -lt $rings; $r++) {
    $y = $r * $segmentLen
    # Per ring, pick two joint indices and blend weights.
    $jf = [single]$r * ($jointCount - 1) / ($rings - 1)
    $j0 = [int][Math]::Floor($jf)
    $j1 = [Math]::Min($j0 + 1, $jointCount - 1)
    $w1 = $jf - [single]$j0
    $w0 = 1.0 - $w1
    for ($s = 0; $s -lt $sides; $s++) {
        $a = ($s / $sides) * 2.0 * [Math]::PI
        $cx = $radius * [Math]::Cos($a)
        $cz = $radius * [Math]::Sin($a)
        $pos.Add([single]$cx); $pos.Add([single]$y); $pos.Add([single]$cz)
        $nrm.Add([single][Math]::Cos($a)); $nrm.Add([single]0.0); $nrm.Add([single][Math]::Sin($a))
        $uv.Add([single]($s / $sides))
        $uv.Add([single]($r / ($rings - 1)))
        $ji.Add([byte]$j0); $ji.Add([byte]$j1); $ji.Add([byte]0); $ji.Add([byte]0)
        $jw.Add([single]$w0); $jw.Add([single]$w1); $jw.Add([single]0.0); $jw.Add([single]0.0)
    }
}
for ($r = 0; $r -lt $rings - 1; $r++) {
    for ($s = 0; $s -lt $sides; $s++) {
        $sn = ($s + 1) % $sides
        $a = $r       * $sides + $s
        $b = $r       * $sides + $sn
        $c = ($r + 1) * $sides + $sn
        $d = ($r + 1) * $sides + $s
        # CCW from outside.
        $idx.Add([uint16]$a); $idx.Add([uint16]$b); $idx.Add([uint16]$c)
        $idx.Add([uint16]$a); $idx.Add([uint16]$c); $idx.Add([uint16]$d)
    }
}
$vertexCount = $pos.Count / 3
$indexCount  = $idx.Count

# --- Inverse bind matrices ---------------------------------------------
# Bind pose: joint j sits at world (0, j*segmentLen, 0). Inverse bind
# is the translation that maps that world position back to the origin
# so a vertex at bind-pose (x, j*L + dy, z) lands at (x, dy, z) inside
# the joint's local frame, which is what the GPU shader's skin math
# expects when it multiplies `jointWorld * inverseBind`.
$ibm = New-Object System.Collections.Generic.List[single]
for ($j = 0; $j -lt $jointCount; $j++) {
    # Column-major 4x4 with -j*segmentLen as the translation Y.
    $m = @(1.0, 0.0, 0.0, 0.0,
           0.0, 1.0, 0.0, 0.0,
           0.0, 0.0, 1.0, 0.0,
           0.0, -($j * $segmentLen), 0.0, 1.0)
    foreach ($v in $m) { $ibm.Add([single]$v) }
}

# --- Animation: 32 keyframes, rotation around +Z per joint ------------
$animKeys = 32
$animDur  = 2.0
$times = New-Object System.Collections.Generic.List[single]
for ($k = 0; $k -lt $animKeys; $k++) {
    $times.Add([single]($k * $animDur / ($animKeys - 1)))
}
# Per-joint rotation output (quaternion x,y,z,w per key). Each key
# rotates the joint around +Z by a phase-shifted sine, same shape the
# old hardcoded procedural rod used so the visual recall is intentional.
$jointRot = @()
for ($j = 0; $j -lt $jointCount; $j++) {
    $jointRot += , (New-Object System.Collections.Generic.List[single])
}
for ($k = 0; $k -lt $animKeys; $k++) {
    $t = $k * $animDur / ($animKeys - 1)
    for ($j = 0; $j -lt $jointCount; $j++) {
        $angle = 0.55 * [Math]::Sin($t * 2.4 + $j * 1.1)
        $half = $angle * 0.5
        $jointRot[$j].Add([single]0.0)
        $jointRot[$j].Add([single]0.0)
        $jointRot[$j].Add([single][Math]::Sin($half))
        $jointRot[$j].Add([single][Math]::Cos($half))
    }
}

# --- Pack the .bin -----------------------------------------------------
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms

function PadTo([System.IO.BinaryWriter]$bw, [int]$alignment) {
    while (($bw.BaseStream.Position % $alignment) -ne 0) { $bw.Write([byte]0) }
}

PadTo $bw 4
$posOffset = [int]$ms.Position
foreach ($v in $pos) { $bw.Write([single]$v) }
PadTo $bw 4
$nrmOffset = [int]$ms.Position
foreach ($v in $nrm) { $bw.Write([single]$v) }
PadTo $bw 4
$uvOffset = [int]$ms.Position
foreach ($v in $uv)  { $bw.Write([single]$v) }
PadTo $bw 4
$jiOffset = [int]$ms.Position
foreach ($v in $ji)  { $bw.Write([byte]$v) }
PadTo $bw 4
$jwOffset = [int]$ms.Position
foreach ($v in $jw)  { $bw.Write([single]$v) }
PadTo $bw 4
$idxOffset = [int]$ms.Position
foreach ($v in $idx) { $bw.Write([uint16]$v) }
PadTo $bw 4
$ibmOffset = [int]$ms.Position
foreach ($v in $ibm) { $bw.Write([single]$v) }
PadTo $bw 4
$timesOffset = [int]$ms.Position
foreach ($v in $times) { $bw.Write([single]$v) }
$jointRotOffsets = @()
for ($j = 0; $j -lt $jointCount; $j++) {
    PadTo $bw 4
    $jointRotOffsets += [int]$ms.Position
    foreach ($v in $jointRot[$j]) { $bw.Write([single]$v) }
}
$binBytes = $ms.ToArray()

# --- glTF JSON ---------------------------------------------------------
$bufferViews = @()
$accessors   = @()
$nextView = 0
$nextAcc  = 0
function AddView([int]$off, [int]$len, $target) {
    $v = @{ buffer = 0; byteOffset = $off; byteLength = $len }
    if ($target -ne $null) { $v.target = $target }
    $script:bufferViews += $v
    $idx = $script:nextView; $script:nextView++
    return $idx
}
function AddAcc([int]$viewIdx, [int]$count, [string]$type, [int]$componentType,
                $mins, $maxs) {
    $a = @{ bufferView = $viewIdx; componentType = $componentType; count = $count; type = $type }
    if ($mins) { $a.min = @(); foreach ($v in $mins) { $a.min += [single]$v } }
    if ($maxs) { $a.max = @(); foreach ($v in $maxs) { $a.max += [single]$v } }
    $script:accessors += $a
    $idx = $script:nextAcc; $script:nextAcc++
    return $idx
}

# Compute position min/max for the engine's culling-friendly accessor.
$xMin = [single]::MaxValue; $yMin = $xMin; $zMin = $xMin
$xMax = [single]::MinValue; $yMax = $xMax; $zMax = $xMax
for ($i = 0; $i -lt $vertexCount; $i++) {
    $x = $pos[$i * 3 + 0]; $y = $pos[$i * 3 + 1]; $z = $pos[$i * 3 + 2]
    if ($x -lt $xMin) { $xMin = $x }; if ($x -gt $xMax) { $xMax = $x }
    if ($y -lt $yMin) { $yMin = $y }; if ($y -gt $yMax) { $yMax = $y }
    if ($z -lt $zMin) { $zMin = $z }; if ($z -gt $zMax) { $zMax = $z }
}

$posByteLen = $vertexCount * 12
$nrmByteLen = $vertexCount * 12
$uvByteLen  = $vertexCount * 8
$jiByteLen  = $vertexCount * 4
$jwByteLen  = $vertexCount * 16
$idxByteLen = $indexCount  * 2
$ibmByteLen = $jointCount  * 64
$timesByteLen = $animKeys * 4
$rotByteLen = $animKeys * 16

$posView = AddView $posOffset $posByteLen 34962
$nrmView = AddView $nrmOffset $nrmByteLen 34962
$uvView  = AddView $uvOffset  $uvByteLen  34962
$jiView  = AddView $jiOffset  $jiByteLen  34962
$jwView  = AddView $jwOffset  $jwByteLen  34962
$idxView = AddView $idxOffset $idxByteLen 34963
$ibmView = AddView $ibmOffset $ibmByteLen $null
$timesView = AddView $timesOffset $timesByteLen $null
$rotViews  = @()
for ($j = 0; $j -lt $jointCount; $j++) {
    $rotViews += AddView $jointRotOffsets[$j] $rotByteLen $null
}

$posAcc = AddAcc $posView $vertexCount 'VEC3' 5126 @($xMin, $yMin, $zMin) @($xMax, $yMax, $zMax)
$nrmAcc = AddAcc $nrmView $vertexCount 'VEC3' 5126 $null $null
$uvAcc  = AddAcc $uvView  $vertexCount 'VEC2' 5126 $null $null
$jiAcc  = AddAcc $jiView  $vertexCount 'VEC4' 5121 $null $null
$jwAcc  = AddAcc $jwView  $vertexCount 'VEC4' 5126 $null $null
$idxAcc = AddAcc $idxView $indexCount  'SCALAR' 5123 $null $null
$ibmAcc = AddAcc $ibmView $jointCount  'MAT4' 5126 $null $null
$timesAcc = AddAcc $timesView $animKeys 'SCALAR' 5126 @([single]0) @([single]$animDur)
$rotAccs = @()
for ($j = 0; $j -lt $jointCount; $j++) {
    $rotAccs += AddAcc $rotViews[$j] $animKeys 'VEC4' 5126 $null $null
}

# Skeleton: node 0 is the model root holding the mesh, nodes 1..4 are
# the four stacked joints. Each joint sits at +Y from its parent.
$nodes = @(
    @{ mesh = 0; skin = 0; children = @(1) },
    @{ translation = @([single]0.0, [single]0.0, [single]0.0); children = @(2) },
    @{ translation = @([single]0.0, [single]$segmentLen, [single]0.0); children = @(3) },
    @{ translation = @([single]0.0, [single]$segmentLen, [single]0.0); children = @(4) },
    @{ translation = @([single]0.0, [single]$segmentLen, [single]0.0) }
)

# Build animation channels: one rotation channel per joint.
$animChannels = @()
$animSamplers = @()
for ($j = 0; $j -lt $jointCount; $j++) {
    $animSamplers += @{ input = $timesAcc; output = $rotAccs[$j]; interpolation = 'LINEAR' }
    $animChannels += @{ sampler = $j; target = @{ node = ($j + 1); path = 'rotation' } }
}

$gltf = @{
    asset       = @{ version = '2.0'; generator = 'NaraGame gen-rigged-rod.ps1' }
    buffers     = @( @{ uri = 'rigged-rod.bin'; byteLength = $binBytes.Length } )
    bufferViews = $bufferViews
    accessors   = $accessors
    meshes      = @( @{ primitives = @( @{
        attributes = @{ POSITION = $posAcc; NORMAL = $nrmAcc; TEXCOORD_0 = $uvAcc;
                        JOINTS_0 = $jiAcc;  WEIGHTS_0 = $jwAcc }
        indices = $idxAcc; mode = 4
    } ) } )
    skins       = @( @{ inverseBindMatrices = $ibmAcc; joints = @(1, 2, 3, 4) } )
    nodes       = $nodes
    scenes      = @( @{ nodes = @(0) } )
    scene       = 0
    animations  = @( @{ name = 'wave'; channels = $animChannels; samplers = $animSamplers } )
}

$json = $gltf | ConvertTo-Json -Depth 16
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($gltfPath, $json, $utf8)
[System.IO.File]::WriteAllBytes($binPath, $binBytes)
"wrote $gltfPath ($vertexCount verts, $indexCount indices, $jointCount joints, $animKeys anim keys)"
"wrote $binPath ($($binBytes.Length) bytes)"
