Add-Type -AssemblyName System.Drawing

# Brawl arena background — 26x18 .ntmap on top of a 32x32 demo tileset.
# Layout: grass everywhere, sand path along the centre cross, stone speckles.
#
# Tile ids (matched to demo-tileset.png from tools/tileset-editor/assets):
#   0 grass        1 grass-dark    2 sand
#   3 dirt         4 water-shallow 5 water-deep
#   6 stone        7 stone-dark
$tileW = 32
$tileH = 32
$cols  = 8
$rows  = 4
$W = $cols * $tileW
$H = $rows * $tileH

$bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g   = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighSpeed
$g.Clear([System.Drawing.Color]::FromArgb(0,0,0,0))

# Same palette as the editor's demo so brawl's bg matches what the editor shows.
$bases = @(
    [System.Drawing.Color]::FromArgb(255,80,160,90),   # 0 grass
    [System.Drawing.Color]::FromArgb(255,60,120,80),   # 1 grass dark
    [System.Drawing.Color]::FromArgb(255,160,140,90),  # 2 sand
    [System.Drawing.Color]::FromArgb(255,120,100,70),  # 3 dirt
    [System.Drawing.Color]::FromArgb(255,70,110,170),  # 4 water shallow
    [System.Drawing.Color]::FromArgb(255,40,80,140),   # 5 water deep
    [System.Drawing.Color]::FromArgb(255,140,140,140), # 6 stone
    [System.Drawing.Color]::FromArgb(255,100,100,110), # 7 stone dark
    [System.Drawing.Color]::FromArgb(255,180,80,70),
    [System.Drawing.Color]::FromArgb(255,140,60,50),
    [System.Drawing.Color]::FromArgb(255,200,180,100),
    [System.Drawing.Color]::FromArgb(255,150,130,70),
    [System.Drawing.Color]::FromArgb(255,210,210,220),
    [System.Drawing.Color]::FromArgb(255,170,200,230),
    [System.Drawing.Color]::FromArgb(255,90,150,90),
    [System.Drawing.Color]::FromArgb(255,60,100,60),
    [System.Drawing.Color]::FromArgb(255,200,90,160),
    [System.Drawing.Color]::FromArgb(255,230,210,80),
    [System.Drawing.Color]::FromArgb(255,180,160,140),
    [System.Drawing.Color]::FromArgb(255,150,130,110),
    [System.Drawing.Color]::FromArgb(255,90,90,100),
    [System.Drawing.Color]::FromArgb(255,140,80,40),
    [System.Drawing.Color]::FromArgb(255,80,150,180),
    [System.Drawing.Color]::FromArgb(255,180,160,90),
    [System.Drawing.Color]::FromArgb(255,220,220,200),
    [System.Drawing.Color]::FromArgb(255,160,80,140),
    [System.Drawing.Color]::FromArgb(255,100,160,200),
    [System.Drawing.Color]::FromArgb(255,80,180,150),
    [System.Drawing.Color]::FromArgb(255,180,100,180),
    [System.Drawing.Color]::FromArgb(255,200,150,80),
    [System.Drawing.Color]::FromArgb(255,50,50,55),
    [System.Drawing.Color]::FromArgb(255,200,200,200)
)
for ($i = 0; $i -lt $bases.Length; $i++) {
    $col = $i % $cols
    $row = [int][math]::Floor($i / $cols)
    $x = $col * $tileW
    $y = $row * $tileH
    $base = $bases[$i]
    $border = [System.Drawing.Color]::FromArgb(255,
        [math]::Max(0, $base.R - 40),
        [math]::Max(0, $base.G - 40),
        [math]::Max(0, $base.B - 40))
    $g.FillRectangle((New-Object System.Drawing.SolidBrush($border)),
                     $x, $y, $tileW, $tileH)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush($base)),
                     $x + 1, $y + 1, $tileW - 2, $tileH - 2)
    $hi = [System.Drawing.Color]::FromArgb(255,
        [math]::Min(255, $base.R + 60),
        [math]::Min(255, $base.G + 60),
        [math]::Min(255, $base.B + 60))
    $hb = New-Object System.Drawing.SolidBrush($hi)
    $g.FillRectangle($hb, $x + 6,  $y + 6,  2, 2)
    $g.FillRectangle($hb, $x + 16, $y + 12, 2, 2)
    $g.FillRectangle($hb, $x + 22, $y + 22, 2, 2)
}
$g.Dispose()
$tilesetPath = "C:\Users\naran\source\repos\NaraGame\test\brawl\assets\bg-tileset.png"
$bmp.Save($tilesetPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host ("tileset: " + $tilesetPath)

# --- Generate the .ntmap. ---
$mapW = 26
$mapH = 18
$out = New-Object System.Text.StringBuilder
[void]$out.AppendLine("ntmap 1")
[void]$out.AppendLine("tileset bg-tileset.png 32 32 0 0")
[void]$out.AppendLine("size $mapW $mapH")
[void]$out.AppendLine("layer")

# Build the grid: grass base with a sandy cross-path and stone speckle hot
# spots. Adds visual interest without colliding with brawl's procedural walls.
$rng = New-Object System.Random 1234
for ($y = 0; $y -lt $mapH; $y++) {
    $line = New-Object System.Collections.Generic.List[string]
    for ($x = 0; $x -lt $mapW; $x++) {
        $id = 0
        # checker-tinted grass
        if ((($x + $y) % 2) -eq 0) { $id = 0 } else { $id = 1 }
        # sand cross through centre
        if ($y -ge 8 -and $y -le 9) { $id = 2 }
        if ($x -ge 12 -and $x -le 13) { $id = 2 }
        # occasional dirt patches near corners
        if (($x -lt 3 -and $y -lt 3) -or
            ($x -gt 22 -and $y -gt 14)) {
            if ($rng.Next(0, 3) -eq 0) { $id = 3 }
        }
        # rare stone speckle
        if ($rng.Next(0, 24) -eq 0) { $id = 6 }
        $line.Add($id.ToString())
    }
    [void]$out.AppendLine(($line -join ' '))
}
# NOTE: no `collision` section. Leaving the section out makes BuildArena
# fall back to its procedural wall/water cluster pass, so each run gets a
# fresh layout. To pin a specific layout, paint Wall/Water cells in the
# editor (L key) and Ctrl+S -- the saved .ntmap will include collision and
# the game will honour it.

$ntmap = "C:\Users\naran\source\repos\NaraGame\test\brawl\assets\arena-bg.ntmap"
[System.IO.File]::WriteAllText($ntmap, $out.ToString(),
    (New-Object System.Text.UTF8Encoding $false))
Write-Host ("ntmap:   " + $ntmap)
