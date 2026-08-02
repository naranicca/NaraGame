param([string]$Destination = (Join-Path $PSScriptRoot 'DemoProject'))

$ErrorActionPreference = 'Stop'
$assets = Join-Path $Destination 'Assets'
$scenes = Join-Path $assets 'Scenes'
$scripts = Join-Path $assets 'Scripts'
$settings = Join-Path $Destination 'ProjectSettings'
[IO.Directory]::CreateDirectory($scenes) | Out-Null
[IO.Directory]::CreateDirectory($scripts) | Out-Null
[IO.Directory]::CreateDirectory($settings) | Out-Null

Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Assets/Sprites') `
    -Destination $assets -Recurse -Force

$readme = Get-Content (Join-Path $PSScriptRoot 'README.md') -Raw
$match = [regex]::Match($readme, '(?s)```lua\s*(.*?)\s*```')
if (-not $match.Success) { throw 'Tutorial Lua block was not found.' }
[IO.File]::WriteAllText((Join-Path $scripts 'TurboTrails.lua'),
    $match.Groups[1].Value, (New-Object Text.UTF8Encoding $false))

$guids = @{
    script='10000000-0000-4000-8000-000000000001'
    road='10000000-0000-4000-8000-000000000002'
    sky='10000000-0000-4000-8000-000000000003'
    player='10000000-0000-4000-8000-000000000004'
    rivals='10000000-0000-4000-8000-000000000005'
    shadow='10000000-0000-4000-8000-000000000006'
    digits='10000000-0000-4000-8000-000000000007'
}

function Write-Json([string]$Path, $Value) {
    $json = $Value | ConvertTo-Json -Depth 30 -Compress
    [IO.File]::WriteAllText($Path, $json, (New-Object Text.UTF8Encoding $false))
}
function Texture-Meta([string]$Path,[string]$Guid,[double]$Ppu,$Sheet=$null) {
    $import=@{pixelsPerUnit=$Ppu;pivotMode='Center';customPivot=@(0.5,0.5)
        filterMode='Point';wrapMode='Clamp';alphaIsTransparency=$true
        border=@(0,0,0,0);spriteMode=if($Sheet){'Multiple'}else{'Single'}}
    if($Sheet){$import.spriteSheet=@{sprites=$Sheet}}
    Write-Json "$Path.meta" @{schema=1;guid=$Guid;type='Texture';importSettings=$import}
}
function Slice([string]$Name,[int]$X,[int]$Y,[int]$W,[int]$H) {
    @{name=$Name;rect=@($X,$Y,$W,$H);pivot=@(0.5,0.5);border=@(0,0,0,0)}
}

Write-Json (Join-Path $Destination 'ProjectSettings/project.json') @{
    name='TurboTrailsDemo';version='0.1.0';schema=1
    sortingLayers=@('Background','Default','Foreground','UI')
    mainScene='Scenes/Race.nscene'
}
Write-Json (Join-Path $scripts 'TurboTrails.lua.meta') @{
    schema=1;guid=$guids.script;type='Script';importSettings=@{}
}
$spriteRoot=Join-Path $assets 'Sprites'
Texture-Meta (Join-Path $spriteRoot 'Track/track-strip.png') $guids.road 48
Texture-Meta (Join-Path $spriteRoot 'Track/sky.png') $guids.sky 48
Texture-Meta (Join-Path $spriteRoot 'Karts/player-kart.png') $guids.player 64
Texture-Meta (Join-Path $spriteRoot 'Karts/shadow.png') $guids.shadow 64
Texture-Meta (Join-Path $spriteRoot 'Karts/rival-karts.png') $guids.rivals 64 @(
    (Slice 'Blue' 0 0 64 64),(Slice 'Red' 64 0 64 64))
$digitSlices=@(); for($i=0;$i -lt 10;$i++){$digitSlices+=Slice "Digit$i" ($i*16) 0 16 24}
Texture-Meta (Join-Path $spriteRoot 'UI/digits.png') $guids.digits 64 $digitSlices

function Transform([double]$X,[double]$Y,[double]$Z=0,
                   [double]$SX=1,[double]$SY=1,[double]$SZ=1) {
    @{type='Transform';position=@($X,$Y,$Z);rotation=0;scale=@($SX,$SY,$SZ)}
}
function Sprite([string]$Guid,[string]$Layer='Default',[int]$Order=0,
                [string]$Sub='') {
    @{type='SpriteRenderer';sprite=$Guid;subSprite=$Sub;color=@(1,1,1,1)
      flipX=$false;flipY=$true;drawMode='Simple';size=@(1,1)
      customQuad=$false;quadTL=@(-0.5,0.5);quadTR=@(0.5,0.5)
      quadBR=@(0.5,-0.5);quadBL=@(-0.5,-0.5);uvRect=@(0,0,1,1)
      sortingLayer=$Layer;orderInLayer=$Order}
}
$entities=@(); $id=1
function Entity([string]$Name,$Components,[bool]$Enabled=$true) {
    $script:idValue=$script:id; $script:id++
    @{id=$idValue;name=$Name;enabled=$Enabled;parent=0;components=$Components}
}
$entities+=Entity 'RaceController' @((Transform 0 0),@{type='Script';script=$guids.script;entryFunction='Race'})
$entities+=Entity 'MainCamera' @((Transform 0 0),@{type='Camera';zoom=48;priority=10})
$entities+=Entity 'Sky' @((Transform 0 5.55 0 2.1 2.1 1),(Sprite $guids.sky 'Background' -100))
for($i=1;$i -le 48;$i++) {
    $entities+=Entity ("Road{0:D2}" -f $i) @((Transform 0 0),(Sprite $guids.road 'Background' (-$i)))
}
for($i=1;$i -le 8;$i++) {
    $sub=if($i%2 -eq 0){'Blue'}else{'Red'}
    $entities+=Entity ("Rival{0:D2}" -f $i) @((Transform 0 0),(Sprite $guids.rivals 'Foreground' (20+$i) $sub)) $false
}
$entities+=Entity 'PlayerShadow' @((Transform 0 -5.85),(Sprite $guids.shadow 'Foreground' 90))
$entities+=Entity 'PlayerKart' @((Transform 0 -5.35),(Sprite $guids.player 'Foreground' 100))
$entities+=Entity 'LapDigit' @((Transform -11.5 6.2),(Sprite $guids.digits 'UI' 200 'Digit1'))
$entities+=Entity 'PositionDigit' @((Transform 10.8 6.2),(Sprite $guids.digits 'UI' 200 'Digit1'))
$entities+=Entity 'SpeedDigit1' @((Transform -0.5 -6.7),(Sprite $guids.digits 'UI' 200 'Digit0'))
$entities+=Entity 'SpeedDigit2' @((Transform 0 -6.7),(Sprite $guids.digits 'UI' 200 'Digit0'))
$entities+=Entity 'SpeedDigit3' @((Transform 0.5 -6.7),(Sprite $guids.digits 'UI' 200 'Digit0'))

Write-Json (Join-Path $scenes 'Race.nscene') @{
    schema=1;sceneName='Race';nextId=$id;entities=$entities
}
Write-Host "Demo project created: $Destination"
