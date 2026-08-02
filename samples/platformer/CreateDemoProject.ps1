param([string]$Destination = (Join-Path $PSScriptRoot 'DemoProject'))

$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false)
$assets = Join-Path $Destination 'Assets'
$scenes = Join-Path $assets 'Scenes'
$scripts = Join-Path $assets 'Scripts'
$settings = Join-Path $Destination 'ProjectSettings'
[IO.Directory]::CreateDirectory($scenes) | Out-Null
[IO.Directory]::CreateDirectory($scripts) | Out-Null
[IO.Directory]::CreateDirectory($settings) | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Assets/Sprites') -Destination $assets -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Assets/Sounds') -Destination $assets -Recurse -Force

function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 30 -Compress), $utf8)
}
function Get-NamedLuaBlocks {
    $lines = [IO.File]::ReadAllLines((Join-Path $PSScriptRoot 'README.md'), [Text.Encoding]::UTF8)
    $inside=$false; $current=[Collections.Generic.List[string]]::new()
    foreach($line in $lines) {
        if(-not $inside -and $line -eq '```lua'){$inside=$true;$current=[Collections.Generic.List[string]]::new();continue}
        if($inside -and $line -eq '```'){
            $text=$current -join "`n"
            if($text -match '(?m)^-- Assets/Scripts/([^\r\n]+)'){
                $name=$Matches[1].Trim(); [IO.File]::WriteAllText((Join-Path $scripts $name),$text,$utf8)
            }
            $inside=$false;continue
        }
        if($inside){$current.Add($line)}
    }
}
Get-NamedLuaBlocks

$guids=@{
 player='31000000-0000-4000-8000-000000000001'; moss='31000000-0000-4000-8000-000000000002'
 coin='31000000-0000-4000-8000-000000000003'; brick='31000000-0000-4000-8000-000000000004'
 gift='31000000-0000-4000-8000-000000000005'; platform='31000000-0000-4000-8000-000000000006'
 sky='31000000-0000-4000-8000-000000000007'; mountain='31000000-0000-4000-8000-000000000008'
 trees='31000000-0000-4000-8000-000000000009'; digits='31000000-0000-4000-8000-000000000010'
 heart='31000000-0000-4000-8000-000000000011'
}
$scriptNames=@('Player','Camera','Coin','Parallax','Mossling','Hud','Platform','Brick','SurpriseBlock')
for($i=0;$i -lt $scriptNames.Count;$i++){
    $guid="31000000-0000-4000-8001-{0:D12}" -f ($i+1)
    $guids[$scriptNames[$i]]=$guid
    Write-Json (Join-Path $scripts ($scriptNames[$i]+'.lua.meta')) @{schema=1;guid=$guid;type='Script';importSettings=@{}}
}
function Slice($Name,$X,$Y,$W,$H){@{name=$Name;rect=@($X,$Y,$W,$H);pivot=@(0.5,0.5);border=@(0,0,0,0)}}
function Texture-Meta($Relative,$Guid,$Ppu,$Slices=$null){
    $path=Join-Path $assets $Relative
    $import=@{pixelsPerUnit=$Ppu;pivotMode='Center';customPivot=@(0.5,0.5);filterMode='Point';wrapMode='Clamp';alphaIsTransparency=$true;border=@(0,0,0,0);spriteMode=if($Slices){'Multiple'}else{'Single'}}
    if($Slices){$import.spriteSheet=@{sprites=$Slices}}
    Write-Json "$path.meta" @{schema=1;guid=$Guid;type='Texture';importSettings=$import}
}
Texture-Meta 'Sprites/player.png' $guids.player 32 @((Slice 'idle_0' 0 0 32 32),(Slice 'run_0' 32 0 32 32),(Slice 'run_1' 64 0 32 32),(Slice 'jump_0' 96 0 32 32))
Texture-Meta 'Sprites/mossling.png' $guids.moss 32 @((Slice 'walk_0' 0 0 32 32),(Slice 'walk_1' 32 0 32 32))
Texture-Meta 'Sprites/coin.png' $guids.coin 32
Texture-Meta 'Sprites/brick.png' $guids.brick 32
Texture-Meta 'Sprites/surprise_block.png' $guids.gift 32 @((Slice 'full' 0 0 32 32),(Slice 'empty' 32 0 32 32))
Texture-Meta 'Sprites/platform.png' $guids.platform 32
Texture-Meta 'Sprites/sky.png' $guids.sky 32
Texture-Meta 'Sprites/mountain.png' $guids.mountain 32
Texture-Meta 'Sprites/trees.png' $guids.trees 32
$digits=@();for($i=0;$i -lt 10;$i++){$digits+=Slice "digit_$i" ($i*16) 0 16 24}
Texture-Meta 'Sprites/digits.png' $guids.digits 32 $digits
Texture-Meta 'Sprites/heart.png' $guids.heart 32

Write-Json (Join-Path $settings 'project.json') @{schema=1;name='PlatformerDemo';version='0.1.0';mainScene='Scenes/Level1.nscene';sortingLayers=@('Background','Default','Actors','Foreground','UI')}
function Transform($X,$Y,$SX=1,$SY=1){@{type='Transform';position=@($X,$Y,0);rotation=0;scale=@($SX,$SY,1)}}
function Sprite($Guid,$Layer='Default',$Order=0,$Sub=''){@{type='SpriteRenderer';sprite=$Guid;subSprite=$Sub;color=@(1,1,1,1);flipX=$false;flipY=$true;drawMode='Simple';size=@(1,1);customQuad=$false;quadTL=@(-0.5,0.5);quadTR=@(0.5,0.5);quadBR=@(0.5,-0.5);quadBL=@(-0.5,-0.5);uvRect=@(0,0,1,1);sortingLayer=$Layer;orderInLayer=$Order}}
function Body($GravityY=-25){@{type='Rigidbody2D';velocity=@(0,0);gravity=@(0,$GravityY);drag=0;mass=1;restitution=0;friction=0}}
function Box($W,$H,$Trigger=$false){@{type='BoxCollider';size=@($W,$H);offset=@(0,0);isTrigger=$Trigger;layer='Default'}}
function Script($Name,$Entry=$Name){@{type='Script';script=$guids[$Name];entryFunction=$Entry}}
$entities=@();$id=1
function Entity($Name,$Components,$Enabled=$true,$Parent=0){$value=$script:id;$script:id++;@{id=$value;name=$Name;enabled=$Enabled;parent=$Parent;components=$Components}}
$entities+=Entity 'Sky' @((Transform 0 1 .65 1.2),(Sprite $guids.sky 'Background' -30),(Script 'Parallax'))
$entities+=Entity 'Mountain' @((Transform 0 -0.7 .65 1),(Sprite $guids.mountain 'Background' -20),(Script 'Parallax'))
$entities+=Entity 'Trees' @((Transform 0 -1.9 .65 1),(Sprite $guids.trees 'Background' -10),(Script 'Parallax'))
$entities+=Entity 'MainCamera' @((Transform 0 0),@{type='Camera';zoom=52;priority=10},(Script 'Camera'))
$entities+=Entity 'GroundVisual' @((Transform 0 -4 18 1),(Sprite $guids.brick 'Default' 0))
$entities+=Entity 'Ground' @((Transform 0 -4),(Box 18 1))
$entities+=Entity 'LeftLedge' @((Transform -7 -1.8 4 0.7),(Sprite $guids.brick 'Default' 1),(Box 1 1))
$entities+=Entity 'RightLedge' @((Transform 7 -0.8 5 0.7),(Sprite $guids.brick 'Default' 1),(Box 1 1))
$entities+=Entity 'Player' @((Transform -3 -2.5 1.5 1.5),(Sprite $guids.player 'Actors' 10 'idle_0'),(Body),(Box 0.7 0.9),(Script 'Player'))
$entities+=Entity 'Mossling' @((Transform 2 -2.5 1.4 1.4),(Sprite $guids.moss 'Actors' 9 'walk_0'),(Body),(Box 0.75 0.7),(Script 'Mossling'))
for($i=1;$i -le 5;$i++){$entities+=Entity "Coin$i" @((Transform (-2+$i*1.2) (-1.5+[Math]::Sin($i)*0.5) 1.2 1.2),(Sprite $guids.coin 'Actors' 8),(Box 0.55 0.55 $true),(Script 'Coin'))}
$entities+=Entity 'MovingPlatform' @((Transform -1 0.4),(Sprite $guids.platform 'Foreground' 3),(Box 3 0.45),(Script 'Platform'))
$entities+=Entity 'Brick01' @((Transform 3 1 1.2 1.2),(Sprite $guids.brick 'Foreground' 3),(Box 0.9 0.9),(Script 'Brick'))
$entities+=Entity 'GiftBlock01' @((Transform 4.2 1 1.2 1.2),(Sprite $guids.gift 'Foreground' 3 'full'),(Box 0.9 0.9),(Script 'SurpriseBlock'))
$entities+=Entity 'GiftCoin01' @((Transform 4.2 2.2 1.2 1.2),(Sprite $guids.coin 'Actors' 8),(Box 0.55 0.55 $true),(Script 'Coin')) $false
$entities+=Entity 'HUD' @((Transform 0 0),(Script 'Hud')) $true 4
for($i=0;$i -lt 3;$i++){$entities+=Entity "ScoreD$i" @((Transform (-7.4+$i*0.55) 4.1 1.3 1.3),(Sprite $guids.digits 'UI' 100 'digit_0')) $true 4}
for($i=0;$i -lt 2;$i++){$entities+=Entity "StageD$i" @((Transform (-0.3+$i*0.55) 4.1 1.3 1.3),(Sprite $guids.digits 'UI' 100 $(if($i -eq 0){'digit_1'}else{'digit_0'}))) $true 4}
for($i=0;$i -lt 3;$i++){$entities+=Entity "LifeH$i" @((Transform (6.1+$i*0.75) 4.1 1.3 1.3),(Sprite $guids.heart 'UI' 100)) $true 4}
Write-Json (Join-Path $scenes 'Level1.nscene') @{schema=1;sceneName='Level1';nextId=$id;entities=$entities}
Write-Json (Join-Path $scenes 'Level1.nscene.meta') @{schema=1;guid='31000000-0000-4000-8000-999999999999';type='Scene';importSettings=@{}}
Write-Host "Platformer demo created: $Destination"
