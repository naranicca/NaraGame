# Procedurally generate tiles.png + characters.png for the zelda test game.
# Re-run any time the visual style or tile budget changes; outputs are checked in.
#
# tiles.png layout (8 cols x 18 rows = 144 cells of 32x32 each, image 256x576):
#   id   0           : grass (floor)
#   ids  1 ..  47    : stone wall (blob47, same canonical mask order as the editor)
#   ids 48 ..  94    : water     (blob47)
#   id  95           : door floor (brown with arch)
#   id  96           : locked door (door + padlock motif)
#   ids 97 .. 143    : dirt path (blob47)
#
# characters.png layout (5 cells of 32x32 = 160x32):
#   id 0: player, 1: slime, 2: key, 3: heart, 4: treasure chest
#
# Blob47 mask convention (matches EditorApp.cpp AutoMaskToVariantTable):
#   bit0=N  bit1=NE  bit2=E  bit3=SE  bit4=S  bit5=SW  bit6=W  bit7=NW
#   Corner bits are suppressed unless BOTH adjacent cardinals are also set.

Add-Type -AssemblyName System.Drawing

# ---- blob47 canonical order: replicate the editor's table[m] ordering ----
function Get-BlobVariantMasks {
    $variants = New-Object System.Collections.Generic.List[int]
    for ($m = 0; $m -lt 256; $m++) {
        $n  = ($m -band 0x01) -ne 0
        $ne = ($m -band 0x02) -ne 0
        $e  = ($m -band 0x04) -ne 0
        $se = ($m -band 0x08) -ne 0
        $s  = ($m -band 0x10) -ne 0
        $sw = ($m -band 0x20) -ne 0
        $w  = ($m -band 0x40) -ne 0
        $nw = ($m -band 0x80) -ne 0
        # Canonicalize: a corner without both flanking cardinals collapses to 0.
        if ($ne -and -not ($n -and $e)) { continue }
        if ($se -and -not ($s -and $e)) { continue }
        if ($sw -and -not ($s -and $w)) { continue }
        if ($nw -and -not ($n -and $w)) { continue }
        [void]$variants.Add($m)
    }
    return ,$variants
}

function New-Color($a, $r, $g, $b) {
    return [System.Drawing.Color]::FromArgb($a, $r, $g, $b)
}

function Fill-Rect($graphics, $color, $x, $y, $w, $h) {
    $brush = New-Object System.Drawing.SolidBrush($color)
    $graphics.FillRectangle($brush, $x, $y, $w, $h)
    $brush.Dispose()
}

# Render one autotile cell at (x,y) with the given 8-bit mask.
# Walls have a thick edge wherever the mask is clear; the inner body is the
# base colour; corners that aren't fully connected get a small notch.
function Draw-AutotileCell {
    param(
        $graphics, $x, $y, $size, $mask,
        $bodyColor, $edgeColor, $accentColor
    )

    Fill-Rect $graphics $bodyColor $x $y $size $size

    $n  = ($mask -band 0x01) -ne 0
    $ne = ($mask -band 0x02) -ne 0
    $e  = ($mask -band 0x04) -ne 0
    $se = ($mask -band 0x08) -ne 0
    $s  = ($mask -band 0x10) -ne 0
    $sw = ($mask -band 0x20) -ne 0
    $w  = ($mask -band 0x40) -ne 0
    $nw = ($mask -band 0x80) -ne 0

    $bar = [int]($size / 8)            # 4px on 32px tile
    if (-not $n)  { Fill-Rect $graphics $edgeColor $x ($y) $size $bar }
    if (-not $s)  { Fill-Rect $graphics $edgeColor $x ($y + $size - $bar) $size $bar }
    if (-not $w)  { Fill-Rect $graphics $edgeColor $x $y $bar $size }
    if (-not $e)  { Fill-Rect $graphics $edgeColor ($x + $size - $bar) $y $bar $size }
    # Inner notch on corners that ARE connected on both sides but lack the diagonal:
    $cn = $bar
    if ($n -and $e -and -not $ne) {
        Fill-Rect $graphics $accentColor ($x + $size - $cn) $y $cn $cn
    }
    if ($s -and $e -and -not $se) {
        Fill-Rect $graphics $accentColor ($x + $size - $cn) ($y + $size - $cn) $cn $cn
    }
    if ($s -and $w -and -not $sw) {
        Fill-Rect $graphics $accentColor $x ($y + $size - $cn) $cn $cn
    }
    if ($n -and $w -and -not $nw) {
        Fill-Rect $graphics $accentColor $x $y $cn $cn
    }
}

# ---- tiles.png ----
$tileW = 32
$cols  = 8
$rows  = 18
$W = $cols * $tileW
$H = $rows * $tileW

$bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g   = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighSpeed
$g.Clear((New-Color 0 0 0 0))

# Tile 0: grass.
$grassBase   = New-Color 255 96  168 88
$grassShade  = New-Color 255 76  144 72
$grassHi     = New-Color 255 130 196 110
Fill-Rect $g $grassBase 0 0 $tileW $tileW
# Speckle pattern.
foreach ($p in @(@(4,6),@(12,14),@(22,8),@(8,22),@(20,24),@(26,18))) {
    Fill-Rect $g $grassHi $p[0] $p[1] 2 2
}
foreach ($p in @(@(2,18),@(16,4),@(28,28))) {
    Fill-Rect $g $grassShade $p[0] $p[1] 2 2
}

$variants = Get-BlobVariantMasks
if ($variants.Count -ne 47) {
    Write-Host ("ERROR: expected 47 blob variants, got " + $variants.Count) -ForegroundColor Red
    exit 1
}

# Tiles 1-47: stone walls.
$wallBody   = New-Color 255 150 144 140
$wallEdge   = New-Color 255  64  60  60
$wallAccent = New-Color 255  90  82  78
for ($i = 0; $i -lt $variants.Count; $i++) {
    $id  = 1 + $i
    $col = $id % $cols
    $row = [int][math]::Floor($id / $cols)
    Draw-AutotileCell $g ($col * $tileW) ($row * $tileW) $tileW $variants[$i] `
        $wallBody $wallEdge $wallAccent
}

# Tiles 48-94: water.
$waterBody   = New-Color 255  60 130 200
$waterEdge   = New-Color 255 220 240 255
$waterAccent = New-Color 255 160 200 235
for ($i = 0; $i -lt $variants.Count; $i++) {
    $id  = 48 + $i
    $col = $id % $cols
    $row = [int][math]::Floor($id / $cols)
    Draw-AutotileCell $g ($col * $tileW) ($row * $tileW) $tileW $variants[$i] `
        $waterBody $waterEdge $waterAccent
}

# Tile 95: door floor (brown stripe with arch hint).
$id = 95
$col = $id % $cols
$row = [int][math]::Floor($id / $cols)
$x = $col * $tileW; $y = $row * $tileW
Fill-Rect $g $grassBase $x $y $tileW $tileW
Fill-Rect $g (New-Color 255 130 90 60) ($x + 6) ($y + 4)  20 26   # door body
Fill-Rect $g (New-Color 255  90 60 36) ($x + 6) ($y + 4)  20  3   # top trim (arch hint)
Fill-Rect $g (New-Color 255  60 40 20) ($x + 14) ($y + 18) 4  6   # knob shadow
Fill-Rect $g (New-Color 255 220 180 60) ($x + 14) ($y + 16) 4  3  # gold knob

# Tile 96: locked door.
$id = 96
$col = $id % $cols
$row = [int][math]::Floor($id / $cols)
$x = $col * $tileW; $y = $row * $tileW
Fill-Rect $g $grassBase $x $y $tileW $tileW
Fill-Rect $g (New-Color 255 110 80 60) ($x + 6) ($y + 4) 20 26
Fill-Rect $g (New-Color 255  70 50 36) ($x + 6) ($y + 4) 20  3
# Padlock body.
Fill-Rect $g (New-Color 255 200 180 70) ($x + 12) ($y + 14) 8 9
Fill-Rect $g (New-Color 255  80  60 20) ($x + 14) ($y + 18) 4 3
# Shackle (U).
Fill-Rect $g (New-Color 255 200 200 200) ($x + 13) ($y + 10) 6 2
Fill-Rect $g (New-Color 255 200 200 200) ($x + 13) ($y + 12) 2 2
Fill-Rect $g (New-Color 255 200 200 200) ($x + 17) ($y + 12) 2 2

# Tiles 97-143: dirt path. Warm tan body with darker brown edge bars so the
# path reads as a sunken trail across grass; subtle accent at inner corners
# keeps the autotile transitions clean.
$dirtBody   = New-Color 255 178 134  86
$dirtEdge   = New-Color 255  92  60  34
$dirtAccent = New-Color 255 138  98  56
for ($i = 0; $i -lt $variants.Count; $i++) {
    $id  = 97 + $i
    $col = $id % $cols
    $row = [int][math]::Floor($id / $cols)
    Draw-AutotileCell $g ($col * $tileW) ($row * $tileW) $tileW $variants[$i] `
        $dirtBody $dirtEdge $dirtAccent
}

$g.Dispose()
$dst = "$PSScriptRoot\tiles.png"
$bmp.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host ("saved: " + $dst)

# ---- characters.png ----
$cellW = 32
$count = 5
$bmp2 = New-Object System.Drawing.Bitmap(($cellW * $count), $cellW,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g2 = [System.Drawing.Graphics]::FromImage($bmp2)
$g2.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
$g2.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighSpeed
$g2.Clear((New-Color 0 0 0 0))

# 0: Player — green tunic, beige face, simple shape.
$px = 0
Fill-Rect $g2 (New-Color 255 80 150 80) ($px + 10) 14 12 14   # body tunic
Fill-Rect $g2 (New-Color 255 230 200 160) ($px + 12) 6 8 8     # face
Fill-Rect $g2 (New-Color 255 60 100 60) ($px + 10) 4 12 4      # hat
Fill-Rect $g2 (New-Color 255 220 180 80) ($px + 13) 8 2 2      # eye L
Fill-Rect $g2 (New-Color 255 220 180 80) ($px + 17) 8 2 2      # eye R
Fill-Rect $g2 (New-Color 255 60 110 60) ($px + 8) 18 4 6       # arm L
Fill-Rect $g2 (New-Color 255 60 110 60) ($px + 20) 18 4 6      # arm R
Fill-Rect $g2 (New-Color 255 60 40 30) ($px + 12) 28 3 3       # foot L
Fill-Rect $g2 (New-Color 255 60 40 30) ($px + 17) 28 3 3       # foot R

# 1: Slime — cyan blob.
$px = 32
Fill-Rect $g2 (New-Color 255 80 200 180) ($px + 8) 14 16 14   # body
Fill-Rect $g2 (New-Color 255 100 220 200) ($px + 6) 18 20 10   # belly skirt
Fill-Rect $g2 (New-Color 255 60 160 140) ($px + 6) 28 20 2     # base shadow
Fill-Rect $g2 (New-Color 255 20 20 30) ($px + 12) 20 2 3       # eye L
Fill-Rect $g2 (New-Color 255 20 20 30) ($px + 18) 20 2 3       # eye R
Fill-Rect $g2 (New-Color 255 240 240 240) ($px + 14) 16 4 2    # shine

# 2: Key.
$px = 64
Fill-Rect $g2 (New-Color 255 230 200 60) ($px + 10) 10 6 6     # bow (head)
Fill-Rect $g2 (New-Color 255 160 130 30) ($px + 12) 12 2 2     # bow hole
Fill-Rect $g2 (New-Color 255 230 200 60) ($px + 12) 16 2 12    # shaft
Fill-Rect $g2 (New-Color 255 230 200 60) ($px + 14) 22 4 2     # tooth1
Fill-Rect $g2 (New-Color 255 230 200 60) ($px + 14) 26 3 2     # tooth2

# 3: Heart.
$px = 96
Fill-Rect $g2 (New-Color 255 220 50 60) ($px + 8) 10 6 6        # left bump
Fill-Rect $g2 (New-Color 255 220 50 60) ($px + 18) 10 6 6       # right bump
Fill-Rect $g2 (New-Color 255 220 50 60) ($px + 8) 14 16 8       # body
Fill-Rect $g2 (New-Color 255 220 50 60) ($px + 10) 22 12 3      # body 2
Fill-Rect $g2 (New-Color 255 220 50 60) ($px + 12) 25 8 2       # tip 1
Fill-Rect $g2 (New-Color 255 220 50 60) ($px + 14) 27 4 2       # tip 2
Fill-Rect $g2 (New-Color 255 255 180 180) ($px + 10) 11 2 2     # shine

# 4: Treasure chest.
$px = 128
Fill-Rect $g2 (New-Color 255 110 70 30) ($px + 6) 14 20 14     # box body
Fill-Rect $g2 (New-Color 255 90 56 24) ($px + 6) 28 20 2       # box base shade
Fill-Rect $g2 (New-Color 255 140 90 40) ($px + 6) 8 20 8       # lid
Fill-Rect $g2 (New-Color 255 90 56 24) ($px + 6) 14 20 2       # lid seam
Fill-Rect $g2 (New-Color 255 240 200 60) ($px + 6) 18 20 2     # gold band 1
Fill-Rect $g2 (New-Color 255 240 200 60) ($px + 6) 24 20 2     # gold band 2
Fill-Rect $g2 (New-Color 255 240 200 60) ($px + 15) 16 2 6     # lock

$g2.Dispose()
$dst2 = "$PSScriptRoot\characters.png"
$bmp2.Save($dst2, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp2.Dispose()
Write-Host ("saved: " + $dst2)
