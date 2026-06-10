# Generate room1.ntmap .. room4.ntmap from ASCII layouts embedded below.
# Recomputes blob47 variant ids for # (wall) and ~ (water) based on each
# cell's 8-neighbour mask so the rooms render with proper autotile edges.
#
# ASCII characters:
#   .  grass (tile 0)
#   #  stone wall (tile 1-47, blob47 variant)
#   ~  water (tile 48-94, blob47 variant; blocks movement)
#   p  dirt path (tile 97-143, blob47 variant; walkable, purely visual)
#   >  door floor (tile 95) + D marker (data from $doors)
#   <  door floor + D marker
#   ^  door floor + D marker
#   v  door floor + D marker
#   *  locked door (tile 96) + L marker (data 'needs=key')
#   @  grass + player spawn marker (@)
#   e  grass + slime marker (E)
#   k  grass + key pickup marker (K)
#   t  grass + treasure marker (T)
#   r  grass + slime spawner marker (placeholder; unused for now)
#
# Door markers per room carry destination metadata via the $doors hashtable
# keyed by '<x>,<y>' (so multiple doors per room are unambiguous).
#
# The collision section is emitted alongside the layer so the game can do
# wall + water blocking without re-deriving it from tile ids.

function Get-BlobVariantTable {
    # Returns a hashtable: canonicalMask -> 0..46 index, plus a parallel array
    # of the canonical masks in index order. Matches AutoMaskToVariantTable()
    # in EditorApp.cpp exactly.
    $table = @{}
    $masks = New-Object System.Collections.Generic.List[int]
    for ($m = 0; $m -lt 256; $m++) {
        $n  = ($m -band 0x01) -ne 0
        $ne = ($m -band 0x02) -ne 0
        $e  = ($m -band 0x04) -ne 0
        $se = ($m -band 0x08) -ne 0
        $s  = ($m -band 0x10) -ne 0
        $sw = ($m -band 0x20) -ne 0
        $w  = ($m -band 0x40) -ne 0
        $nw = ($m -band 0x80) -ne 0
        if ($ne -and -not ($n -and $e)) { continue }
        if ($se -and -not ($s -and $e)) { continue }
        if ($sw -and -not ($s -and $w)) { continue }
        if ($nw -and -not ($n -and $w)) { continue }
        $table[$m] = $masks.Count
        [void]$masks.Add($m)
    }
    return @{ table = $table; masks = $masks }
}

function Canonicalize-Mask {
    param([int]$mask)
    $n  = ($mask -band 0x01) -ne 0
    $e  = ($mask -band 0x04) -ne 0
    $s  = ($mask -band 0x10) -ne 0
    $w  = ($mask -band 0x40) -ne 0
    $m = $mask
    if (-not ($n -and $e)) { $m = $m -band (-bnot 0x02) }
    if (-not ($s -and $e)) { $m = $m -band (-bnot 0x08) }
    if (-not ($s -and $w)) { $m = $m -band (-bnot 0x20) }
    if (-not ($n -and $w)) { $m = $m -band (-bnot 0x80) }
    return $m
}

function Compute-VariantId {
    param([int]$baseId, [int]$mask, $variantTable)
    $canon = Canonicalize-Mask $mask
    if (-not $variantTable.table.ContainsKey($canon)) { return $baseId }
    return $baseId + $variantTable.table[$canon]
}

# Build mask from a 2D char grid at (x,y) for cells matching $sameChars.
function Compute-Mask {
    param($lines, $x, $y, $sameChars)
    $h = $lines.Count
    $w = $lines[0].Length
    function Same($lx, $ly) {
        if ($lx -lt 0 -or $ly -lt 0 -or $lx -ge $w -or $ly -ge $h) { return $true }
        return $sameChars -contains $lines[$ly][$lx]
    }
    $m = 0
    if (Same $x        ($y - 1)) { $m = $m -bor 0x01 }
    if (Same ($x + 1)  ($y - 1)) { $m = $m -bor 0x02 }
    if (Same ($x + 1)  $y      ) { $m = $m -bor 0x04 }
    if (Same ($x + 1)  ($y + 1)) { $m = $m -bor 0x08 }
    if (Same $x        ($y + 1)) { $m = $m -bor 0x10 }
    if (Same ($x - 1)  ($y + 1)) { $m = $m -bor 0x20 }
    if (Same ($x - 1)  $y      ) { $m = $m -bor 0x40 }
    if (Same ($x - 1)  ($y - 1)) { $m = $m -bor 0x80 }
    return $m
}

# ---- Room definitions ----
$room1 = @{
    name = 'room1'
    layout = @(
        '####################'
        '#..................#'
        '#....~~~~~~........#'
        '#...~~~~~~~~.......#'
        '#....~~~~~~........#'
        '#..................#'
        '#.@.ppppppppppppppp>'
        '#..pppppppppppppppp>'
        '#..................#'
        '#........e.........#'
        '#..................#'
        '####################')
    doors = @{
        '19,6' = 'to=room2,side=west'
        '19,7' = 'to=room2,side=west'
    }
}

$room2 = @{
    name = 'room2'
    layout = @(
        '#########**#########'
        '#~~......pp........#'
        '#~~~.....pp........#'
        '#~~~.....pp........#'
        '#~~......pp........#'
        '#..e.....pp...e....#'
        '<pppppppppppppppppp>'
        '<pppppppppppppppppp>'
        '#........pp........#'
        '#........pp........#'
        '#........pp........#'
        '####################')
    doors = @{
        '9,0'   = 'to=room3,side=south'
        '10,0'  = 'to=room3,side=south'
        '0,6'   = 'to=room1,side=east'
        '0,7'   = 'to=room1,side=east'
        '19,6'  = 'to=room4,side=west'
        '19,7'  = 'to=room4,side=west'
    }
}

# Note: the '*' tiles at (9,0)/(10,0) of room2 are locked doors. They are
# placed there but the door transition data above includes only the
# unlocked doors that face south. We keep '*' separate -- the L markers
# already get 'needs=key' from the converter, and the destination data is
# duplicated on those keys too so the L door knows where to take you.

$room3 = @{
    name = 'room3'
    layout = @(
        '####################'
        '#~~~~~~~~~~~~......#'
        '#~~~~~~~~~~~~..e...#'
        '#~~~~~~~~~~~~......#'
        '#~~~~~~~~~~~~......#'
        '#..................#'
        '#...ppppp...k......#'
        '#...p...p..........#'
        '#...p.e.p..........#'
        '#...ppppp..........#'
        '#########^##########'
        '####################')
    doors = @{
        '9,10' = 'to=room2,side=north'
    }
}

$room4 = @{
    name = 'room4'
    layout = @(
        '####################'
        '#~~~~~~~~..........#'
        '#~~~~~~~~..........#'
        '#~~~~..............#'
        '#~~................#'
        '<...e.pppppppppp...#'
        '<.....p........p...#'
        '#..e..p...t....p...#'
        '#.....p........p...#'
        '#.....pppppppppp...#'
        '#..................#'
        '####################')
    doors = @{
        '0,5' = 'to=room2,side=east'
        '0,6' = 'to=room2,side=east'
    }
}

$rooms = @($room1, $room2, $room3, $room4)
$variantTable = Get-BlobVariantTable

# Special-case the locked-door cells in room2 -- they share x,y with door
# transitions south, so merge those into the locked-door data via 'L'.
# The L marker gets the destination + the lock requirement.
$lockedDoorDestinations = @{
    'room2-9,0'  = 'needs=key,to=room3,side=south'
    'room2-10,0' = 'needs=key,to=room3,side=south'
}

foreach ($room in $rooms) {
    $layout = $room.layout
    $w = $layout[0].Length
    $h = $layout.Count
    $tiles  = New-Object 'int[]' ($w * $h)
    $coll   = New-Object 'byte[]' ($w * $h)
    $objects = New-Object System.Collections.Generic.List[string]

    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $layout[$y][$x]
            $idx = $y * $w + $x
            $tile  = 0
            $colByte = 0
            $marker  = $null
            $markerData = $null

            switch ($c) {
                '.' { $tile = 0; $colByte = 0 }
                '#' {
                    $mask = Compute-Mask $layout $x $y @('#')
                    $tile = Compute-VariantId 1 $mask $variantTable
                    $colByte = 1
                }
                '~' {
                    $mask = Compute-Mask $layout $x $y @('~')
                    $tile = Compute-VariantId 48 $mask $variantTable
                    $colByte = 2
                }
                'p' {
                    # Dirt path connects to itself only (not grass) for the
                    # mask -- so adjacent grass cells get the "edge" variants.
                    $mask = Compute-Mask $layout $x $y @('p')
                    $tile = Compute-VariantId 97 $mask $variantTable
                    $colByte = 0
                }
                '>' { $tile = 95; $colByte = 0; $marker = 'D' }
                '<' { $tile = 95; $colByte = 0; $marker = 'D' }
                '^' { $tile = 95; $colByte = 0; $marker = 'D' }
                'v' { $tile = 95; $colByte = 0; $marker = 'D' }
                '*' { $tile = 96; $colByte = 0; $marker = 'L' }
                '@' { $tile = 0;  $colByte = 0; $marker = '@' }
                'e' { $tile = 0;  $colByte = 0; $marker = 'E' }
                'k' { $tile = 0;  $colByte = 0; $marker = 'K' }
                't' { $tile = 0;  $colByte = 0; $marker = 'T' }
                default {
                    Write-Host ("warn: unknown char '" + $c + "' at " + $x + "," + $y + " in " + $room.name)
                    $tile = 0; $colByte = 0
                }
            }
            $tiles[$idx] = $tile
            $coll[$idx]  = [byte]$colByte
            if ($marker -ne $null) {
                $key = "$x,$y"
                if ($marker -eq 'D') {
                    $markerData = $room.doors[$key]
                    if (-not $markerData) { $markerData = '' }
                } elseif ($marker -eq 'L') {
                    $lk = $room.name + '-' + $key
                    if ($lockedDoorDestinations.ContainsKey($lk)) {
                        $markerData = $lockedDoorDestinations[$lk]
                    } else {
                        $markerData = 'needs=key'
                    }
                }
                $line = "$marker $x $y"
                if ($markerData) { $line = $line + ' ' + $markerData }
                [void]$objects.Add($line)
            }
        }
    }

    # ---- Assemble .ntmap text ----
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("ntmap 1")
    [void]$sb.AppendLine("tileset tiles.png 32 32 0 0")
    [void]$sb.AppendLine("size $w $h")
    [void]$sb.AppendLine("layer")
    for ($y = 0; $y -lt $h; $y++) {
        $rowParts = @()
        for ($x = 0; $x -lt $w; $x++) {
            $rowParts += [string]$tiles[$y * $w + $x]
        }
        [void]$sb.AppendLine([string]::Join(' ', $rowParts))
    }
    [void]$sb.AppendLine("collision")
    for ($y = 0; $y -lt $h; $y++) {
        $rowParts = @()
        for ($x = 0; $x -lt $w; $x++) {
            $rowParts += [string][int]$coll[$y * $w + $x]
        }
        [void]$sb.AppendLine([string]::Join(' ', $rowParts))
    }
    if ($objects.Count -gt 0) {
        [void]$sb.AppendLine("objects")
        foreach ($line in $objects) { [void]$sb.AppendLine($line) }
    }
    [void]$sb.AppendLine("symbols")
    [void]$sb.AppendLine("@ 7fff7f player")
    [void]$sb.AppendLine("E ff5050 slime")
    [void]$sb.AppendLine("K ffe040 key pickup")
    [void]$sb.AppendLine("D ff8030 door")
    [void]$sb.AppendLine("L 808080 locked door")
    [void]$sb.AppendLine("T ffd040 treasure")
    [void]$sb.AppendLine("autotile")
    [void]$sb.AppendLine("1 47 blob47")
    [void]$sb.AppendLine("48 47 blob47")
    [void]$sb.AppendLine("97 47 blob47")

    $dst = "$PSScriptRoot\$($room.name).ntmap"
    # Force UTF-8 without BOM -- the engine's stdlib reader doesn't strip BOMs.
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($dst, $sb.ToString(), $utf8)
    Write-Host ("saved: " + $dst)
}
