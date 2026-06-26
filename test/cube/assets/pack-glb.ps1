# Pack a .gltf + sidecar .bin pair into a single .glb (glTF binary, v2).
#
# Usage:
#   ./pack-glb.ps1 -GltfPath sphere.gltf
#     -> writes sphere.glb next to it (uses buffers[0].uri to find the .bin)
#   ./pack-glb.ps1 -GltfPath rigged-rod.gltf -OutPath foo.glb
#
# Container layout (glTF 2.0 spec):
#   header   = 'glTF' (4B) + version=2 (4B LE u32) + totalLength (4B LE u32)
#   chunk    = length (4B) + type (4B) + data (length bytes)
#   chunk 0  = JSON  -- the glTF JSON text, padded with 0x20 to 4B align
#   chunk 1  = BIN   -- the buffer bytes,    padded with 0x00 to 4B align
#
# The .glb has no external buffer, so buffers[0].uri is stripped from the
# JSON before packing -- the loader treats buffers[0] as the BIN chunk.

param(
    [Parameter(Mandatory = $true)]
    [string] $GltfPath,
    [string] $OutPath = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $GltfPath)) {
    throw "pack-glb: '$GltfPath' not found"
}

$gltfFull = (Resolve-Path $GltfPath).Path
$gltfDir  = Split-Path -Parent $gltfFull

# Read JSON text and pull the .bin uri so we can read the buffer.
$jsonText = Get-Content -Raw -LiteralPath $gltfFull
$uriMatch = [regex]::Match($jsonText, '"uri"\s*:\s*"([^"]+)"')
if (-not $uriMatch.Success) {
    throw "pack-glb: '$GltfPath' has no buffers[0].uri -- already a .glb?"
}
$binName = $uriMatch.Groups[1].Value
$binFull = Join-Path $gltfDir $binName
if (-not (Test-Path $binFull)) {
    throw "pack-glb: buffer '$binFull' not found"
}

# Strip the `"uri": "...",` field (with its trailing comma + whitespace) so
# the embedded buffers[0] has only byteLength -- what the .glb spec wants.
$stripped = [regex]::Replace($jsonText, '"uri"\s*:\s*"[^"]+"\s*,\s*', '')

# Pad JSON with spaces so the BIN chunk starts on a 4-byte boundary.
# PowerShell's `+` on two byte[]s widens to Object[], which makes
# BinaryWriter pick the wrong overload and silently truncate the write;
# allocate a fresh byte[] of the padded length and copy in instead.
$jsonRaw   = [System.Text.Encoding]::UTF8.GetBytes($stripped)
$jsonPad   = (4 - ($jsonRaw.Length % 4)) % 4
$jsonBytes = New-Object byte[] ($jsonRaw.Length + $jsonPad)
[Array]::Copy($jsonRaw, 0, $jsonBytes, 0, $jsonRaw.Length)
for ($i = 0; $i -lt $jsonPad; $i++) {
    $jsonBytes[$jsonRaw.Length + $i] = 0x20  # ' '
}

# Read and pad BIN with zeros to 4 bytes (zeros already from New-Object).
$binRaw   = [System.IO.File]::ReadAllBytes($binFull)
$binPad   = (4 - ($binRaw.Length % 4)) % 4
$binBytes = New-Object byte[] ($binRaw.Length + $binPad)
[Array]::Copy($binRaw, 0, $binBytes, 0, $binRaw.Length)

# Header + chunk headers (8 each) + payloads.
$totalLength = 12 + 8 + $jsonBytes.Length + 8 + $binBytes.Length

# Emit .glb.
if ([string]::IsNullOrEmpty($OutPath)) {
    $OutPath = [System.IO.Path]::ChangeExtension($gltfFull, ".glb")
}
$stream = [System.IO.File]::Open($OutPath, [System.IO.FileMode]::Create)
try {
    $writer = New-Object System.IO.BinaryWriter($stream)
    try {
        # Header: 'glTF', version=2, totalLength.
        $writer.Write([byte[]](0x67, 0x6C, 0x54, 0x46))
        $writer.Write([uint32] 2)
        $writer.Write([uint32] $totalLength)
        # JSON chunk: length, type='JSON', data.
        $writer.Write([uint32] $jsonBytes.Length)
        $writer.Write([byte[]](0x4A, 0x53, 0x4F, 0x4E))
        $writer.Write($jsonBytes)
        # BIN chunk: length, type='BIN\0', data.
        $writer.Write([uint32] $binBytes.Length)
        $writer.Write([byte[]](0x42, 0x49, 0x4E, 0x00))
        $writer.Write($binBytes)
    } finally {
        $writer.Dispose()
    }
} finally {
    $stream.Dispose()
}

Write-Host ("pack-glb: {0} -> {1} ({2} bytes; JSON {3}, BIN {4})" -f `
    (Split-Path -Leaf $gltfFull),
    (Split-Path -Leaf $OutPath),
    $totalLength,
    $jsonBytes.Length,
    $binBytes.Length)
