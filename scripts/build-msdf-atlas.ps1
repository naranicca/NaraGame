# Bakes an MSDF atlas from a TTF using msdf-atlas-gen (in tools/).
# Run when the source font or atlas parameters change. The output
# msdf-inter.{png,json} pair is committed; the binary and TTF are not.
#
# Usage:
#   .\scripts\build-msdf-atlas.ps1                  # default: Inter -> flappy
#   .\scripts\build-msdf-atlas.ps1 -Size 64         # bump atlas EM size
$ErrorActionPreference = 'Stop'
param(
    [int]    $Size    = 48,
    [int]    $PxRange = 4,
    [string] $Font    = "tools\Inter-Regular.ttf",
    [string] $OutDir  = "test\flappy\assets",
    [string] $OutName = "msdf-inter"
)

$root = (Resolve-Path "$PSScriptRoot\..").Path
$exe  = Join-Path $root "tools\msdf-atlas-gen.exe"
$ttf  = Join-Path $root $Font
$png  = Join-Path $root "$OutDir\$OutName.png"
$json = Join-Path $root "$OutDir\$OutName.json"

if (-not (Test-Path $exe)) { throw "Missing $exe -- run scripts\fetch-msdf-tool.ps1 first." }
if (-not (Test-Path $ttf)) { throw "Missing $ttf -- run scripts\fetch-msdf-tool.ps1 first." }

# yorigin top matches the engine's top-left screen-space coords; the loader
# doesn't have to flip planeBounds. pxrange is baked into the JSON so the
# shader can read it (or for now we hard-code 4 in the shader).
& $exe `
    -font     $ttf `
    -type     msdf `
    -format   png `
    -size     $Size `
    -pxrange  $PxRange `
    -yorigin  top `
    -imageout $png `
    -json     $json

if ($LASTEXITCODE -ne 0) { throw "msdf-atlas-gen failed (exit $LASTEXITCODE)" }
"wrote $png ($((Get-Item $png).Length) bytes)"
"wrote $json ($((Get-Item $json).Length) bytes)"
