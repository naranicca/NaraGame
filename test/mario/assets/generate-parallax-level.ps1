# Extend tiles.png with a cloud + tree tile, then rewrite level1.ntmap
# with three tile layers:
#   layer 0 (base, parallax 1.0): the original gameplay platforms
#   layer 1 (parallax 0.3):       clouds drifting through the sky
#   layer 2 (parallax 1.5):       occasional trees in the foreground
#
# Rerun this script whenever the tileset or level layout changes; it is
# the source of truth for both files and overwrites them in place.

Add-Type -AssemblyName System.Drawing

$assetDir   = $PSScriptRoot
$tilesPng   = Join-Path $assetDir 'tiles.png'
$levelPath  = Join-Path $assetDir 'level1.ntmap'

# --- 1. Extend the tileset with a cloud tile + a tree tile ------------------
# Original tileset: 352x32 with 11 cells. Add 2 cells -> 416x32 / 13 cells.
$origBmp = New-Object System.Drawing.Bitmap $tilesPng
$origW   = $origBmp.Width
$origH   = $origBmp.Height
$cellW   = 32
$cellH   = 32
$newCells = 2
$newBmp = New-Object System.Drawing.Bitmap ($origW + $cellW * $newCells), $origH
$g = [System.Drawing.Graphics]::FromImage($newBmp)
$g.Clear([System.Drawing.Color]::Transparent)
$g.DrawImage($origBmp, 0, 0, $origW, $origH)

# Cloud tile: simple white pixel-art cloud on transparent. Drawn one
# pixel at a time so it reads correctly at the game's nearest-neighbour
# filter.
$cloudX = $origW
$cloud = @(
    '................................',
    '................................',
    '................................',
    '..........WWWWWWWW..............',
    '........WWWWWWWWWWWWW...........',
    '.......WWWWWWWWWWWWWWW..........',
    '....WWWWWWWWWWWWWWWWWWWW........',
    '..WWWWWWWWWWWWWWWWWWWWWWWW......',
    '..WWWWWWWWWWWWWWWWWWWWWWWWWW....',
    '..WWWWWWWWWWWWWWWWWWWWWWWWWWWW..',
    '..WWWWWWWWWWWWWWWWWWWWWWWWWWWW..',
    '..WWWWWWWWWWWWWWWWWWWWWWWWWWWW..',
    '..WWWWWWWWWWWWWWWWWWWWWWWWWWWW..',
    '...WWWWWWWWWWWWWWWWWWWWWWWWWW...',
    '....WWWWWWWWWWWWWWWWWWWWWWWW....',
    '......WWWWWWWWWWWWWWWWWWWWW.....',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................',
    '................................'
)
$white = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
for ($y = 0; $y -lt $cellH; $y++) {
    $row = $cloud[$y]
    for ($x = 0; $x -lt $cellW; $x++) {
        if ($row[$x] -eq 'W') {
            $newBmp.SetPixel($cloudX + $x, $y, $white)
        }
    }
}

# Tree tile: a stylised dark silhouette so the FG plane reads as "in
# front of" the playfield without obscuring too much. Trunk on the
# bottom rows, two layered crowns above.
$treeX = $origW + $cellW
$tree = @(
    '..............TT................',
    '............TTTTTT..............',
    '...........TTTTTTTT.............',
    '..........TTTTTTTTTT............',
    '.........TTTTTTTTTTTT...........',
    '........TTTTTTTTTTTTTT..........',
    '.......TTTTTTTTTTTTTTTT.........',
    '......TTTTTTTTTTTTTTTTTT........',
    '......TTTTTTTTTTTTTTTTTT........',
    '.....TTTTTTTTTTTTTTTTTTTT.......',
    '....TTTTTTTTTTTTTTTTTTTTTT......',
    '....TTTTTTTTTTTTTTTTTTTTTT......',
    '...TTTTTTTTTTTTTTTTTTTTTTTT.....',
    '...TTTTTTTTTTTTTTTTTTTTTTTT.....',
    '..TTTTTTTTTTTTTTTTTTTTTTTTTT....',
    '..TTTTTTTTTTTTTTTTTTTTTTTTTT....',
    '.TTTTTTTTTTTTTTTTTTTTTTTTTTTT...',
    '.TTTTTTTTTTTTTTTTTTTTTTTTTTTT...',
    '..TTTTTTTTTTTTTTTTTTTTTTTTTT....',
    '...TTTTTTTTTTTTTTTTTTTTTTTT.....',
    '.....TTTTTTTTTTTTTTTTTTTT.......',
    '.........TTTTTTTTTTTTT..........',
    '............TTBBTTTT............',
    '..............BBBB..............',
    '..............BBBB..............',
    '..............BBBB..............',
    '..............BBBB..............',
    '..............BBBB..............',
    '..............BBBB..............',
    '..............BBBB..............',
    '..............BBBB..............',
    '..............BBBB..............'
)
$leaf  = [System.Drawing.Color]::FromArgb(255,  24,  44,  20)
$trunk = [System.Drawing.Color]::FromArgb(255,  35,  20,  10)
for ($y = 0; $y -lt $cellH; $y++) {
    $row = $tree[$y]
    for ($x = 0; $x -lt $cellW; $x++) {
        $c = $row[$x]
        if     ($c -eq 'T') { $newBmp.SetPixel($treeX + $x, $y, $leaf)  }
        elseif ($c -eq 'B') { $newBmp.SetPixel($treeX + $x, $y, $trunk) }
    }
}

$g.Dispose()
$origBmp.Dispose()
$newBmp.Save($tilesPng, [System.Drawing.Imaging.ImageFormat]::Png)
$newBmp.Dispose()
"updated $tilesPng (now $($cellW * 13)x$cellH, 13 tiles)"

# --- 2. Rewrite level1.ntmap with two extra layers + parallax ---------------
$cloudTile = 11
$treeTile  = 12

# Read existing file. Capture: tileset line, size, base layer rows,
# collision rows, objects, etc. Keep them verbatim so we don't
# accidentally drop content.
$lines = Get-Content $levelPath
$mapW = 0; $mapH = 0
$header = @()        # header through `size` line
$baseGrid = @()      # mapH lines of `layer` content
$collisionLines = @()
$objectsLines = @()
$tail = @()          # symbols / autotile / other directives

$section = 'pre'    # pre | layer | collision | objects | tail
$layerCount = 0
foreach ($line in $lines) {
    if ($line -match '^size\s+(\d+)\s+(\d+)') {
        $mapW = [int]$Matches[1]; $mapH = [int]$Matches[2]
    }
    if ($section -eq 'pre') {
        $header += $line
        if ($line -match '^layer\s*$') {
            $section = 'layer'
            continue
        }
        continue
    }
    if ($section -eq 'layer') {
        if ($line -match '^[A-Za-z]') {
            # New directive after the layer block. Route to whichever
            # section it announces.
            if ($line -match '^collision\s*$')     { $section = 'collision'; continue }
            elseif ($line -match '^objects\s*$')   { $section = 'objects';   continue }
            elseif ($line -match '^symbols\s*$' -or
                    $line -match '^autotile\s*$' -or
                    $line -match '^parallax\s*$' -or
                    $line -match '^layer\s*$') {
                $section = 'tail'; $tail += $line; continue
            }
        }
        if ($line -ne '' -and -not ($line -match '^#')) { $baseGrid += $line }
        continue
    }
    if ($section -eq 'collision') {
        if ($line -match '^[A-Za-z]') {
            if ($line -match '^objects\s*$') { $section = 'objects'; continue }
            $section = 'tail'; $tail += $line; continue
        }
        if ($line -ne '' -and -not ($line -match '^#')) { $collisionLines += $line }
        continue
    }
    if ($section -eq 'objects') {
        # objects block is line-oriented. End on a top-level directive.
        if ($line -match '^[A-Za-z]+\s*$' -and
            $line -notmatch '^[A-Z]\s+\d') {
            $section = 'tail'; $tail += $line; continue
        }
        $objectsLines += $line
        continue
    }
    $tail += $line
}

if ($baseGrid.Count -ne $mapH) {
    throw "expected $mapH base-grid rows, got $($baseGrid.Count)"
}

# Build clouds layer: every Nth column, drop a cloud sprite somewhere in
# the upper third of the map. Single-tile clouds keep the math simple
# and look fine through the parallax shift.
$cloudLayer = New-Object 'int[,]' $mapH, $mapW
for ($y = 0; $y -lt $mapH; $y++) {
    for ($x = 0; $x -lt $mapW; $x++) {
        $cloudLayer[$y, $x] = -1
    }
}
$cloudPositions = @(
    @(  5, 2), @( 14, 3), @( 22, 1), @( 31, 4),
    @( 41, 2), @( 50, 3), @( 60, 1), @( 70, 4),
    @( 79, 2), @( 88, 3), @( 96, 1), @(104, 4)
)
foreach ($p in $cloudPositions) {
    $cx = $p[0]; $cy = $p[1]
    if ($cx -ge 0 -and $cx -lt $mapW -and $cy -ge 0 -and $cy -lt $mapH) {
        $cloudLayer[$cy, $cx] = $cloudTile
    }
}

# Build tree layer: sparse trees on the foreground plane. They render
# faster than the camera (parallax 1.5) so they sweep past quickly --
# put them in the lower-middle band where they'll occlude part of the
# play field as they pass.
$treeLayer = New-Object 'int[,]' $mapH, $mapW
for ($y = 0; $y -lt $mapH; $y++) {
    for ($x = 0; $x -lt $mapW; $x++) {
        $treeLayer[$y, $x] = -1
    }
}
$treePositions = @(
    @(  8, 12), @( 20, 12), @( 35, 12),
    @( 55, 12), @( 73, 12), @( 92, 12)
)
foreach ($p in $treePositions) {
    $tx = $p[0]; $ty = $p[1]
    if ($tx -ge 0 -and $tx -lt $mapW -and $ty -ge 0 -and $ty -lt $mapH) {
        $treeLayer[$ty, $tx] = $treeTile
    }
}

# --- 3. Emit the new file ---------------------------------------------------
$out = New-Object System.Collections.Generic.List[string]
foreach ($h in $header) { [void]$out.Add($h) }
foreach ($row in $baseGrid) { [void]$out.Add($row) }
[void]$out.Add('layer')
for ($y = 0; $y -lt $mapH; $y++) {
    $cells = @()
    for ($x = 0; $x -lt $mapW; $x++) { $cells += $cloudLayer[$y, $x].ToString() }
    [void]$out.Add(($cells -join ' '))
}
[void]$out.Add('layer')
for ($y = 0; $y -lt $mapH; $y++) {
    $cells = @()
    for ($x = 0; $x -lt $mapW; $x++) { $cells += $treeLayer[$y, $x].ToString() }
    [void]$out.Add(($cells -join ' '))
}
if ($collisionLines.Count -gt 0) {
    [void]$out.Add('collision')
    foreach ($c in $collisionLines) { [void]$out.Add($c) }
}
if ($objectsLines.Count -gt 0) {
    [void]$out.Add('objects')
    foreach ($o in $objectsLines) { [void]$out.Add($o) }
}
foreach ($t in $tail) { [void]$out.Add($t) }
[void]$out.Add('parallax')
[void]$out.Add('1.0')
[void]$out.Add('0.3')
[void]$out.Add('1.5')

# Use UTF-8 (no BOM) for cross-platform safety -- the loader is line-based
# so a BOM on line 0 would break the "ntmap 1" header check.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($levelPath, $out, $utf8NoBom)
"updated $levelPath (3 layers, parallax 1.0/0.3/1.5)"
