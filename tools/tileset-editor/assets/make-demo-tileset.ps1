Add-Type -AssemblyName System.Drawing

# Tiny 8x4 demo tileset of 32 colourful 32x32 tiles. Each tile has a flat
# base, a thin border, and a small motif so they read as distinct in the
# palette without needing real art.

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

# Palette of base hues. 32 entries; cycle through with a row-tinted shift so
# adjacent tiles read different.
$bases = @(
    [System.Drawing.Color]::FromArgb(255,80,160,90),   # grass
    [System.Drawing.Color]::FromArgb(255,60,120,80),   # grass dark
    [System.Drawing.Color]::FromArgb(255,160,140,90),  # sand
    [System.Drawing.Color]::FromArgb(255,120,100,70),  # dirt
    [System.Drawing.Color]::FromArgb(255,70,110,170),  # water shallow
    [System.Drawing.Color]::FromArgb(255,40,80,140),   # water deep
    [System.Drawing.Color]::FromArgb(255,140,140,140), # stone
    [System.Drawing.Color]::FromArgb(255,100,100,110), # stone dark
    [System.Drawing.Color]::FromArgb(255,180,80,70),   # brick
    [System.Drawing.Color]::FromArgb(255,140,60,50),   # brick dark
    [System.Drawing.Color]::FromArgb(255,200,180,100), # wood
    [System.Drawing.Color]::FromArgb(255,150,130,70),  # wood dark
    [System.Drawing.Color]::FromArgb(255,210,210,220), # ice
    [System.Drawing.Color]::FromArgb(255,170,200,230), # snow
    [System.Drawing.Color]::FromArgb(255,90,150,90),   # bush
    [System.Drawing.Color]::FromArgb(255,60,100,60),   # tree
    [System.Drawing.Color]::FromArgb(255,200,90,160),  # flower pink
    [System.Drawing.Color]::FromArgb(255,230,210,80),  # flower yellow
    [System.Drawing.Color]::FromArgb(255,180,160,140), # path
    [System.Drawing.Color]::FromArgb(255,150,130,110), # path dark
    [System.Drawing.Color]::FromArgb(255,90,90,100),   # roof
    [System.Drawing.Color]::FromArgb(255,140,80,40),   # roof brown
    [System.Drawing.Color]::FromArgb(255,80,150,180),  # window
    [System.Drawing.Color]::FromArgb(255,180,160,90),  # door
    [System.Drawing.Color]::FromArgb(255,220,220,200), # cloth
    [System.Drawing.Color]::FromArgb(255,160,80,140),  # carpet
    [System.Drawing.Color]::FromArgb(255,100,160,200), # tile blue
    [System.Drawing.Color]::FromArgb(255,80,180,150),  # tile teal
    [System.Drawing.Color]::FromArgb(255,180,100,180), # tile magenta
    [System.Drawing.Color]::FromArgb(255,200,150,80),  # tile orange
    [System.Drawing.Color]::FromArgb(255,50,50,55),    # void
    [System.Drawing.Color]::FromArgb(255,200,200,200)  # white
)

for ($i = 0; $i -lt $bases.Length; $i++) {
    $col = $i % $cols
    $row = [int][math]::Floor($i / $cols)
    $x = $col * $tileW
    $y = $row * $tileH
    $base = $bases[$i]
    # Border one shade darker.
    $border = [System.Drawing.Color]::FromArgb(255,
        [math]::Max(0, $base.R - 40),
        [math]::Max(0, $base.G - 40),
        [math]::Max(0, $base.B - 40))
    $g.FillRectangle((New-Object System.Drawing.SolidBrush($border)),
                     $x, $y, $tileW, $tileH)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush($base)),
                     $x + 1, $y + 1, $tileW - 2, $tileH - 2)
    # Small motif (2x2 pixel cluster) for variety.
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
$dst = "C:\Users\naran\source\repos\NaraGame\tools\tileset-editor\assets\demo-tileset.png"
$bmp.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host ("saved: " + $dst)
