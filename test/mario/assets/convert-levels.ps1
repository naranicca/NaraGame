# One-shot converter: level{1,2}.txt -> level{1,2}.ntmap.
#
# The original ASCII grid carries two kinds of data at each cell:
#   - tile characters  ( # = b ? ( ) { } X o , space ) — drawn as map tiles
#   - object characters ( M g ! * )                    — game-level markers
# The legend mirrors MarioApp.cpp's GameScene::OnEnter().
#
# `!` (mushroom block) and `*` (fire-flower block) live on the QUESTION tile
# in-game; MarioApp used to promote them at load time. We bake that promotion
# into the saved tile layer so the game just sees a QUESTION tile + a marker.

$ErrorActionPreference = 'Stop'
$AssetsDir = Split-Path -Parent $PSCommandPath

# char -> tile id (matches the Mario `Tile` enum)
$Legend = @{
    [char]'#' = 0   # GROUND
    [char]'=' = 1   # DIRT
    [char]'b' = 2   # BRICK
    [char]'?' = 3   # QUESTION
    [char]'(' = 4   # PIPE_TL
    [char]')' = 5   # PIPE_TR
    [char]'{' = 6   # PIPE_BL
    [char]'}' = 7   # PIPE_BR
    [char]'X' = 8   # HARD
    [char]'o' = 9   # COIN
}
$EmptyId    = -1
$QuestionId = 3
$ObjectChars = @([char]'M', [char]'g', [char]'!', [char]'*')

function Convert-Level {
    param(
        [string]$InPath,
        [string]$OutPath,
        [string]$TilesetRel
    )

    $lines = [IO.File]::ReadAllLines($InPath)
    $rows = $lines.Length
    $cols = ($lines | ForEach-Object { $_.Length } | Measure-Object -Maximum).Maximum

    $tiles   = New-Object 'System.Collections.Generic.List[int]'
    $objects = New-Object 'System.Collections.Generic.List[string]'
    for ($y = 0; $y -lt $rows; $y++) {
        $row = $lines[$y]
        for ($x = 0; $x -lt $cols; $x++) {
            if ($x -lt $row.Length) { $ch = $row[$x] } else { $ch = [char]' ' }
            if ($Legend.ContainsKey($ch)) {
                $tiles.Add($Legend[$ch])
            } elseif ($ObjectChars -contains $ch) {
                # Object markers go in the objects section. `!` and `*` also
                # need a solid QUESTION tile underneath so the game can
                # stand/bump on them; `M` and `g` sit in empty air.
                if ($ch -eq [char]'!' -or $ch -eq [char]'*') {
                    $tiles.Add($QuestionId)
                } else {
                    $tiles.Add($EmptyId)
                }
                $objects.Add(("{0} {1} {2}" -f $ch, $x, $y))
            } elseif ($ch -eq [char]' ') {
                $tiles.Add($EmptyId)
            } else {
                throw "Unknown character '$ch' at ($x,$y) in $InPath"
            }
        }
    }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("ntmap 1")
    [void]$sb.AppendLine("tileset $TilesetRel 32 32 0 0")
    [void]$sb.AppendLine("size $cols $rows")
    [void]$sb.AppendLine("layer")
    for ($y = 0; $y -lt $rows; $y++) {
        $row = New-Object 'System.Collections.Generic.List[string]'
        for ($x = 0; $x -lt $cols; $x++) {
            [void]$row.Add($tiles[$y * $cols + $x].ToString())
        }
        [void]$sb.AppendLine(($row -join ' '))
    }
    if ($objects.Count -gt 0) {
        [void]$sb.AppendLine("objects")
        foreach ($o in $objects) { [void]$sb.AppendLine($o) }
    }

    # BOM-less UTF-8 — the .ntmap parser is byte-oriented and a BOM would
    # land in the first token on line 1 (`\xef\xbb\xbfntmap`).
    [IO.File]::WriteAllText($OutPath, $sb.ToString(),
        (New-Object System.Text.UTF8Encoding $false))
    Write-Host ("wrote " + $OutPath + " (" + $cols + "x" + $rows +
                ", " + $objects.Count + " objects)")
}

Convert-Level -InPath  (Join-Path $AssetsDir 'level1.txt') `
              -OutPath (Join-Path $AssetsDir 'level1.ntmap') `
              -TilesetRel 'tiles.png'
Convert-Level -InPath  (Join-Path $AssetsDir 'level2.txt') `
              -OutPath (Join-Path $AssetsDir 'level2.ntmap') `
              -TilesetRel 'tiles.png'
