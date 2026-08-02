param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [switch]$SkipSounds
)

Add-Type -AssemblyName System.Drawing

function Ensure-Parent([string]$Path) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
}

function Export-Crop([string]$Source, [string]$Dest,
                     [int]$X, [int]$Y, [int]$W, [int]$H,
                     [int]$OutW, [int]$OutH, [bool]$Key = $true) {
    $sourceImage = [Drawing.Bitmap]::FromFile($Source)
    try {
        $output = New-Object Drawing.Bitmap $OutW, $OutH,
            ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [Drawing.Graphics]::FromImage($output)
            try {
                $graphics.InterpolationMode =
                    [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
                $graphics.PixelOffsetMode =
                    [Drawing.Drawing2D.PixelOffsetMode]::Half
                $graphics.DrawImage($sourceImage,
                    (New-Object Drawing.Rectangle 0,0,$OutW,$OutH),
                    $X,$Y,$W,$H,[Drawing.GraphicsUnit]::Pixel)
            } finally { $graphics.Dispose() }
            if ($Key) {
                # Flood-fill only key-coloured pixels connected to an image
                # edge. This removes generated pink/purple cell backgrounds
                # while preserving purple details inside a character.
                $seen=New-Object 'bool[,]' $OutW,$OutH
                $queue=New-Object 'Collections.Generic.Queue[Drawing.Point]'
                function Is-Key([Drawing.Color]$c) {
                    $c.R -gt 105 -and $c.B -gt 85 -and $c.G -lt 115 -and
                    ([Math]::Abs([int]$c.R-[int]$c.B) -lt 135)
                }
                function Add-Key([int]$kx,[int]$ky) {
                    if($kx -lt 0 -or $ky -lt 0 -or $kx -ge $OutW -or $ky -ge $OutH){return}
                    if($seen[$kx,$ky]){return}; $seen[$kx,$ky]=$true
                    if(Is-Key ($output.GetPixel($kx,$ky))){
                        $queue.Enqueue((New-Object Drawing.Point $kx,$ky))
                    }
                }
                for($px=0;$px -lt $OutW;$px++){Add-Key $px 0;Add-Key $px ($OutH-1)}
                for($py=0;$py -lt $OutH;$py++){Add-Key 0 $py;Add-Key ($OutW-1) $py}
                while($queue.Count -gt 0){
                    $point=$queue.Dequeue()
                    # Color.Transparent is A=0 but RGB=255,255,255.  Keep the
                    # hidden channels black as well so straight/premultiplied
                    # alpha renderers cannot expose a white fringe.
                    $output.SetPixel($point.X,$point.Y,[Drawing.Color]::FromArgb(0,0,0,0))
                    Add-Key ($point.X-1) $point.Y; Add-Key ($point.X+1) $point.Y
                    Add-Key $point.X ($point.Y-1); Add-Key $point.X ($point.Y+1)
                }
                # Generated atlas cells have a one-pixel white/magenta guide
                # frame.  Nearest-neighbour downscaling can leave it in the
                # first two output pixels even when the cell background was
                # removed, so clear only the outer perimeter.
                $clear=[Drawing.Color]::FromArgb(0,0,0,0)
                for($edge=0;$edge -lt 2;$edge++){
                    for($px=0;$px -lt $OutW;$px++){
                        $output.SetPixel($px,$edge,$clear)
                        $output.SetPixel($px,$OutH-1-$edge,$clear)
                    }
                    for($py=0;$py -lt $OutH;$py++){
                        $output.SetPixel($edge,$py,$clear)
                        $output.SetPixel($OutW-1-$edge,$py,$clear)
                    }
                }
            }
            Ensure-Parent $Dest
            $output.Save($Dest,[Drawing.Imaging.ImageFormat]::Png)
        } finally { $output.Dispose() }
    } finally { $sourceImage.Dispose() }
}

function Export-StackedCrop([string]$Source, [string]$Dest,
                            [int]$X1,[int]$Y1,[int]$W1,[int]$H1,
                            [int]$X2,[int]$Y2,[int]$W2,[int]$H2,
                            [int]$OutW,[int]$RowH) {
    $sourceImage=[Drawing.Bitmap]::FromFile($Source)
    try {
        $output=New-Object Drawing.Bitmap $OutW,($RowH*2),
            ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics=[Drawing.Graphics]::FromImage($output)
            try {
                $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.DrawImage($sourceImage,(New-Object Drawing.Rectangle 0,0,$OutW,$RowH),
                    $X1,$Y1,$W1,$H1,[Drawing.GraphicsUnit]::Pixel)
                $graphics.DrawImage($sourceImage,(New-Object Drawing.Rectangle 0,$RowH,$OutW,$RowH),
                    $X2,$Y2,$W2,$H2,[Drawing.GraphicsUnit]::Pixel)
            } finally {$graphics.Dispose()}
            Ensure-Parent $Dest
            $output.Save($Dest,[Drawing.Imaging.ImageFormat]::Png)
        } finally {$output.Dispose()}
    } finally {$sourceImage.Dispose()}
}

function New-Wave([string]$Dest, [double[]]$Notes,
                  [double]$Seconds, [double]$Volume = 0.25,
                  [bool]$Loop = $false) {
    $rate=44100; $count=[int]($rate*$Seconds)
    Ensure-Parent $Dest
    $stream=[IO.File]::Create($Dest)
    $writer=New-Object IO.BinaryWriter $stream
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
        $writer.Write([int](36+$count*2))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
        $writer.Write([int]16); $writer.Write([int16]1); $writer.Write([int16]1)
        $writer.Write([int]$rate); $writer.Write([int]($rate*2))
        $writer.Write([int16]2); $writer.Write([int16]16)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
        $writer.Write([int]($count*2))
        for($i=0;$i -lt $count;$i++) {
            $t=$i/$rate; $sample=0.0
            foreach($note in $Notes) { $sample += [Math]::Sin(2*[Math]::PI*$note*$t) }
            $sample /= [Math]::Max(1,$Notes.Count)
            $env = if($Loop){1.0}else{[Math]::Min(1,$t*30)*[Math]::Max(0,1-$t/$Seconds)}
            $writer.Write([int16]([Math]::Round($sample*$env*$Volume*32767)))
        }
    } finally { $writer.Dispose(); $stream.Dispose() }
}

$p=Join-Path $Root 'samples/platformer'; $a=Join-Path $p 'Source/atlas.png'
Export-Crop $a (Join-Path $p 'Assets/Sprites/player.png') 30 62 580 188 128 32
Export-Crop $a (Join-Path $p 'Assets/Sprites/mossling.png') 618 62 352 188 64 32
Export-Crop $a (Join-Path $p 'Assets/Sprites/coin.png') 972 62 158 188 32 32
Export-Crop $a (Join-Path $p 'Assets/Sprites/heart.png') 1132 62 158 188 32 32
Export-Crop $a (Join-Path $p 'Assets/Sprites/logo.png') 1295 62 210 188 128 64
Export-Crop $a (Join-Path $p 'Assets/Sprites/digits.png') 30 260 1475 122 160 24
Export-Crop $a (Join-Path $p 'Assets/Tilesets/tiles.png') 30 394 735 176 128 32
Export-Crop $a (Join-Path $p 'Assets/Sprites/platform.png') 775 394 232 176 96 16
Export-Crop $a (Join-Path $p 'Assets/Sprites/brick.png') 1010 394 193 176 32 32
Export-Crop $a (Join-Path $p 'Assets/Sprites/surprise_block.png') 1205 394 300 176 64 32
Export-Crop $a (Join-Path $p 'Assets/Sprites/sky.png') 30 578 1475 112 960 256 $false
Export-Crop $a (Join-Path $p 'Assets/Sprites/mountain.png') 30 704 1475 114 960 192 $false
Export-Crop $a (Join-Path $p 'Assets/Sprites/trees.png') 30 828 1475 150 960 192 $true

$p=Join-Path $Root 'samples/sky-hopper'; $a=Join-Path $p 'Source/atlas.png'
Export-Crop $a (Join-Path $p 'Assets/Sprites/glider.png') 30 240 330 200 48 32
Export-Crop $a (Join-Path $p 'Assets/Sprites/tower.png') 390 35 170 520 64 256
Export-Crop $a (Join-Path $p 'Assets/Sprites/sky.png') 595 35 910 520 480 720 $false
Export-Crop $a (Join-Path $p 'Assets/Sprites/ground.png') 30 605 1475 130 512 64
Export-Crop $a (Join-Path $p 'Assets/Sprites/clouds.png') 30 775 1475 190 512 128
Copy-Item -LiteralPath (Join-Path $Root 'samples/platformer/Assets/Sprites/digits.png') `
    -Destination (Join-Path $p 'Assets/Sprites/digits.png') -Force

$p=Join-Path $Root 'samples/nightfall-arena'; $a=Join-Path $p 'Source/atlas.png'
Export-Crop $a (Join-Path $p 'Assets/Sprites/ranger.png') 90 88 243 337 48 48
Export-Crop $a (Join-Path $p 'Assets/Sprites/shade.png') 369 88 264 337 48 48
Export-Crop $a (Join-Path $p 'Assets/Sprites/spark.png') 669 88 231 337 16 16
Export-Crop $a (Join-Path $p 'Assets/Sprites/gem.png') 935 88 225 337 16 16
Export-Crop $a (Join-Path $p 'Assets/Sprites/arena_floor.png') 283 483 685 667 256 256 $false
Copy-Item -LiteralPath (Join-Path $Root 'samples/platformer/Assets/Sprites/digits.png') `
    -Destination (Join-Path $p 'Assets/Sprites/digits.png') -Force

$p=Join-Path $Root 'samples/ashen-vale'; $a=Join-Path $p 'Source/atlas.png'
Export-Crop $a (Join-Path $p 'Assets/Sprites/world_atlas.png') 25 20 1410 535 1024 384
Export-Crop $a (Join-Path $p 'Assets/Sprites/title_background.png') 25 578 1485 250 1280 360 $false
Export-Crop $a (Join-Path $p 'Assets/Sprites/ui_atlas.png') 80 845 1320 170 768 128
$menuSource=Join-Path $p 'Source/menu_labels_source.png'
if(Test-Path -LiteralPath $menuSource) {
    $menu=[Drawing.Bitmap]::FromFile($menuSource)
    try {
        $menu.MakeTransparent($menu.GetPixel(0,0))
        $menu.Save((Join-Path $p 'Assets/Sprites/menu_labels.png'),
            [Drawing.Imaging.ImageFormat]::Png)
    } finally { $menu.Dispose() }
}

$p=Join-Path $Root 'samples/turbo-trails'; $a=Join-Path $p 'Source/atlas.png'
Export-Crop $a (Join-Path $p 'Assets/Sprites/Karts/player-kart.png') 15 15 385 345 96 80
Export-Crop $a (Join-Path $p 'Assets/Sprites/Karts/rival-karts.png') 417 15 449 345 128 64
Export-Crop $a (Join-Path $p 'Assets/Sprites/Track/sky.png') 884 15 632 332 640 180 $false
Export-StackedCrop $a (Join-Path $p 'Assets/Sprites/Track/track-strip.png') `
    22 445 1490 16 22 605 1490 16 256 32
Export-Crop $a (Join-Path $p 'Assets/Sprites/UI/digits.png') 15 698 1505 152 160 24
Export-Crop $a (Join-Path $p 'Assets/Sprites/Karts/shadow.png') 15 866 1505 130 64 20

$sounds=@(
    @('platformer','jump.wav',@(523.25,659.25),0.18),
    @('platformer','coin.wav',@(987.77,1318.51),0.16),
    @('platformer','stomp.wav',@(110.0,82.4),0.14),
    @('platformer','hurt.wav',@(180.0,145.0),0.30),
    @('platformer','break.wav',@(95.0,190.0),0.20),
    @('platformer','bgm.wav',@(261.63,329.63,392.0),4.0,$true),
    @('sky-hopper','flap.wav',@(440.0,660.0),0.12),
    @('sky-hopper','score.wav',@(880.0,1174.66),0.18),
    @('sky-hopper','hit.wav',@(120.0,75.0),0.25),
    @('nightfall-arena','hit.wav',@(130.0,90.0),0.18),
    @('nightfall-arena','shoot.wav',@(740.0,1046.5),0.12),
    @('nightfall-arena','levelup.wav',@(523.25,659.25,783.99),0.50),
    @('nightfall-arena','arena.wav',@(110.0,164.81,220.0),4.0,$true),
    @('ashen-vale','sword.wav',@(160.0,320.0),0.14),
    @('ashen-vale','spell.wav',@(440.0,554.37,659.25),0.32),
    @('ashen-vale','hurt.wav',@(125.0,92.0),0.26),
    @('ashen-vale','portal.wav',@(220.0,329.63,493.88),0.55),
    @('ashen-vale','quest.wav',@(659.25,880.0),0.28),
    @('ashen-vale','town.wav',@(220.0,277.18,329.63),4.0,$true),
    @('ashen-vale','dungeon.wav',@(82.41,123.47,164.81),4.0,$true)
)
if(-not $SkipSounds) {
    foreach($s in $sounds) {
        $loop=if($s.Count -gt 4){[bool]$s[4]}else{$false}
        New-Wave (Join-Path $Root ("samples/{0}/Assets/Sounds/{1}" -f $s[0],$s[1])) `
            ([double[]]$s[2]) ([double]$s[3]) 0.22 $loop
    }
}

Write-Host 'Sample assets generated.'
