# Extend tiles.png with a cloud + tree tile + 6 slope tiles, then rewrite
# level1.ntmap with three tile layers:
#   layer 0 (base, parallax 1.0): the original gameplay platforms
#   layer 1 (parallax 0.3):       clouds drifting through the sky
#   layer 2 (parallax 1.5):       occasional trees in the foreground
#
# Rerun this script whenever the tileset or level layout changes; it is
# the source of truth for both files and overwrites them in place.
#
# Tile id layout (after this script runs):
#    0..10  original gameplay (GROUND..USED)
#   11      cloud (BG, parallax 0.3)
#   12      tree  (FG, parallax 1.5)
#   13      slope 45 up-right       (low at left, high at right)
#   14      slope 45 up-left        (high at left, low at right)
#   15      slope 22 up-right A     (left half:  low to mid)
#   16      slope 22 up-right B     (right half: mid to high)
#   17      slope 22 up-left  A     (left half:  high to mid)
#   18      slope 22 up-left  B     (right half: mid to low)
# The slope geometry must match SLOPE_* handling in MarioApp.cpp; edit
# both in lockstep.

Add-Type -AssemblyName System.Drawing

$assetDir   = $PSScriptRoot
$tilesPng   = Join-Path $assetDir 'tiles.png'
$levelPath  = Join-Path $assetDir 'level1.ntmap'

# --- 1. Extend the tileset with cloud + tree + slope tiles ------------------
# Keep this script idempotent: tiles.png on disk may already include the
# extras from a previous run, so trim back to the canonical 11 gameplay
# cells (GROUND..USED) before stacking the new ones on top. Without the
# trim, repeated runs duplicate cloud + tree and shove the slopes past
# their expected ids.
$origBmp = New-Object System.Drawing.Bitmap $tilesPng
$cellW   = 32
$cellH   = 32
$baseCells = 11
$origW   = $cellW * $baseCells
$origH   = $origBmp.Height
$newCells = 8   # 1 cloud + 1 tree + 6 slopes
$newBmp = New-Object System.Drawing.Bitmap ($origW + $cellW * $newCells), $origH
$g = [System.Drawing.Graphics]::FromImage($newBmp)
$g.Clear([System.Drawing.Color]::Transparent)
# Source rectangle clips to the first 11 cells even if the file on disk is
# wider (a previous run's extras); the DrawImage stretches nothing since
# the dest rect matches dimensions.
$srcRect = New-Object System.Drawing.Rectangle 0, 0, $origW, $origH
$dstRect = New-Object System.Drawing.Rectangle 0, 0, $origW, $origH
$g.DrawImage($origBmp, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)

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

# Slope tiles. The fill is a flat dirt/brick brown so they read as
# "solid ground" without needing a separate texture pass. Each tile's
# solid region is bounded by the slope surface; pixels above the line
# stay transparent so the sky / parallax shows through.
$slopeFill = [System.Drawing.Color]::FromArgb(255, 198, 110,  52)
$slopeEdge = [System.Drawing.Color]::FromArgb(255, 152,  72,  20)

# Per-tile slope-surface formula: for each pixel y at column x within
# the cell, returns the y row at which the slope surface lives. The
# pixel is solid iff y >= surface(x). Numbers chosen so the surface
# pixel itself is part of the solid (so the visible top of the slope
# matches the body-snap math in MarioApp::ResolveSlopes).
function Fill-Slope([System.Drawing.Bitmap]$bmp, [int]$ox, [scriptblock]$surface) {
    for ($y = 0; $y -lt $cellH; $y++) {
        for ($x = 0; $x -lt $cellW; $x++) {
            $sy = & $surface $x
            if ($y -ge $sy) {
                # Outline the surface row so the slope edge reads cleanly
                # at zoom 2 instead of fading into the rest of the fill.
                if ($y -lt ($sy + 2)) {
                    $bmp.SetPixel($ox + $x, $y, $slopeEdge)
                } else {
                    $bmp.SetPixel($ox + $x, $y, $slopeFill)
                }
            }
        }
    }
}

# Slope 45 UR: surface = (cellH - 1 - x). At x=0 surface=31, at x=31 surface=0.
$slope45URx = $origW + $cellW * 2
Fill-Slope $newBmp $slope45URx { param($x) $cellH - 1 - $x }

# Slope 45 UL: surface = x. At x=0 surface=0, at x=31 surface=31.
$slope45ULx = $origW + $cellW * 3
Fill-Slope $newBmp $slope45ULx { param($x) $x }

# Slope 22 up-right A (left half): surface from y=cellH-1 at x=0 to
# y=cellH/2 at x=cellW-1.
$slope22URAx = $origW + $cellW * 4
Fill-Slope $newBmp $slope22URAx {
    param($x) [int]($cellH - 1 - ($x / 2.0))
}

# Slope 22 up-right B (right half): surface from y=cellH/2-1 at x=0 to
# y=0 at x=cellW-1.
$slope22URBx = $origW + $cellW * 5
Fill-Slope $newBmp $slope22URBx {
    param($x) [int](($cellH / 2) - 1 - ($x / 2.0))
}

# Slope 22 up-left A (left half): surface from y=0 at x=0 to
# y=cellH/2-1 at x=cellW-1.
$slope22ULAx = $origW + $cellW * 6
Fill-Slope $newBmp $slope22ULAx {
    param($x) [int]($x / 2.0)
}

# Slope 22 up-left B (right half): surface from y=cellH/2 at x=0 to
# y=cellH-1 at x=cellW-1.
$slope22ULBx = $origW + $cellW * 7
Fill-Slope $newBmp $slope22ULBx {
    param($x) [int](($cellH / 2) + ($x / 2.0))
}

$g.Dispose()
$origBmp.Dispose()
$newBmp.Save($tilesPng, [System.Drawing.Imaging.ImageFormat]::Png)
$newBmp.Dispose()
"updated $tilesPng (now $($cellW * 19)x$cellH, 19 tiles)"

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

# --- Parse base layer into a 2D array so we can inject slopes ----------------
$baseTiles = New-Object 'int[,]' $mapH, $mapW
for ($y = 0; $y -lt $mapH; $y++) {
    $cells = $baseGrid[$y].Trim() -split '\s+'
    if ($cells.Count -ne $mapW) {
        throw "row $y has $($cells.Count) cells, expected $mapW"
    }
    for ($x = 0; $x -lt $mapW; $x++) {
        $baseTiles[$y, $x] = [int]$cells[$x]
    }
}

# Inject slope demos onto the gameplay (base) layer. Positions are
# hand-picked spots on flat ground; each demo carves out a one-tile
# elevation change with a matching down-slope on the far side so the
# player can walk up and back down again. SLOPE_* enum values match
# MarioApp.cpp / the tile ids generated above.
$SLOPE_45UR  = 13
$SLOPE_45UL  = 14
$SLOPE_22URA = 15
$SLOPE_22URB = 16
$SLOPE_22ULA = 17
$SLOPE_22ULB = 18
$GROUND      = 0
$DIRT        = 1
# Ground surface row in level1 = 16 (zero-indexed); the row above is
# the elevated platform that the slopes climb to. Filler below the
# elevated platform copies dirt so the cliff doesn't show as void.
$groundRow   = 16
$peakRow     = 15
$dirtRow     = 17

# 45 deg demo around cols 9..14, right at the start so the player
# walks into it within a few seconds:
#   col 10 SLOPE_45UR -> cols 11..12 elevated -> col 13 SLOPE_45UL.
$baseTiles[$groundRow, 10] = $SLOPE_45UR
$baseTiles[$dirtRow,   10] = $DIRT
for ($x = 11; $x -le 12; $x++) {
    $baseTiles[$peakRow,   $x] = $GROUND
    $baseTiles[$groundRow, $x] = $DIRT
    $baseTiles[$dirtRow,   $x] = $DIRT
}
$baseTiles[$groundRow, 13] = $SLOPE_45UL
$baseTiles[$dirtRow,   13] = $DIRT

# 22.5 deg demo around cols 20..27:
# cols 20..21 SLOPE_22UR pair, cols 22..25 elevated, cols 26..27 SLOPE_22UL pair.
$baseTiles[$groundRow, 20] = $SLOPE_22URA
$baseTiles[$dirtRow,   20] = $DIRT
$baseTiles[$groundRow, 21] = $SLOPE_22URB
$baseTiles[$dirtRow,   21] = $DIRT
for ($x = 22; $x -le 25; $x++) {
    $baseTiles[$peakRow,   $x] = $GROUND
    $baseTiles[$groundRow, $x] = $DIRT
    $baseTiles[$dirtRow,   $x] = $DIRT
}
$baseTiles[$groundRow, 26] = $SLOPE_22ULA
$baseTiles[$dirtRow,   26] = $DIRT
$baseTiles[$groundRow, 27] = $SLOPE_22ULB
$baseTiles[$dirtRow,   27] = $DIRT

# Serialize the mutated base back into the line array for the file write.
for ($y = 0; $y -lt $mapH; $y++) {
    $cells = @()
    for ($x = 0; $x -lt $mapW; $x++) { $cells += $baseTiles[$y, $x].ToString() }
    $baseGrid[$y] = ($cells -join ' ')
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
