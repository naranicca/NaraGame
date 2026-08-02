param([string]$Destination=(Join-Path $PSScriptRoot 'DemoProject'))
$ErrorActionPreference='Stop';$utf8=[Text.UTF8Encoding]::new($false)
$assets=Join-Path $Destination 'Assets';$scenes=Join-Path $assets 'Scenes';$scripts=Join-Path $assets 'Scripts';$settings=Join-Path $Destination 'ProjectSettings'
[IO.Directory]::CreateDirectory($scenes)|Out-Null;[IO.Directory]::CreateDirectory($scripts)|Out-Null;[IO.Directory]::CreateDirectory($settings)|Out-Null
Copy-Item (Join-Path $PSScriptRoot 'Assets/Sprites') $assets -Recurse -Force;Copy-Item (Join-Path $PSScriptRoot 'Assets/Sounds') $assets -Recurse -Force
function Json($p,$v){[IO.File]::WriteAllText($p,($v|ConvertTo-Json -Depth 30 -Compress),$utf8)}
$read=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'README.md'),[Text.Encoding]::UTF8);$matches=[regex]::Matches($read,'(?s)```lua\s*(.*?)\s*```')
[IO.File]::WriteAllText((Join-Path $scripts 'SkyHopper.lua'),$matches[0].Groups[1].Value,$utf8);[IO.File]::WriteAllText((Join-Path $scripts 'SkyHud.lua'),$matches[1].Groups[1].Value,$utf8)
$g=@{game='32000000-0000-4000-8000-000000000001';hud='32000000-0000-4000-8000-000000000002';glider='32000000-0000-4000-8000-000000000003';tower='32000000-0000-4000-8000-000000000004';sky='32000000-0000-4000-8000-000000000005';ground='32000000-0000-4000-8000-000000000006';clouds='32000000-0000-4000-8000-000000000007';digits='32000000-0000-4000-8000-000000000008'}
Json (Join-Path $scripts 'SkyHopper.lua.meta') @{schema=1;guid=$g.game;type='Script';importSettings=@{}};Json (Join-Path $scripts 'SkyHud.lua.meta') @{schema=1;guid=$g.hud;type='Script';importSettings=@{}}
function Slice($n,$x,$y,$w,$h){@{name=$n;rect=@($x,$y,$w,$h);pivot=@(.5,.5);border=@(0,0,0,0)}}
function Tex($n,$guid,$ppu,$s=$null){$i=@{pixelsPerUnit=$ppu;pivotMode='Center';customPivot=@(.5,.5);filterMode='Point';wrapMode='Clamp';alphaIsTransparency=$true;border=@(0,0,0,0);spriteMode=if($s){'Multiple'}else{'Single'}};if($s){$i.spriteSheet=@{sprites=$s}};Json ((Join-Path $assets "Sprites/$n")+'.meta') @{schema=1;guid=$guid;type='Texture';importSettings=$i}}
Tex 'glider.png' $g.glider 32;Tex 'tower.png' $g.tower 32;Tex 'sky.png' $g.sky 48;Tex 'ground.png' $g.ground 32;Tex 'clouds.png' $g.clouds 32
$ds=@();for($i=0;$i-lt10;$i++){$ds+=Slice "digit_$i" ($i*16) 0 16 24};Tex 'digits.png' $g.digits 32 $ds
Json (Join-Path $settings 'project.json') @{schema=1;name='SkyHopperDemo';version='0.1.0';mainScene='Scenes/Game.nscene';sortingLayers=@('Background','Default','Actors','Foreground','UI')}
function T($x,$y,$sx=1,$sy=1){@{type='Transform';position=@($x,$y,0);rotation=0;scale=@($sx,$sy,1)}}
function S($guid,$layer='Default',$order=0,$sub=''){@{type='SpriteRenderer';sprite=$guid;subSprite=$sub;color=@(1,1,1,1);flipX=$false;flipY=$true;drawMode='Simple';size=@(1,1);customQuad=$false;quadTL=@(-.5,.5);quadTR=@(.5,.5);quadBR=@(.5,-.5);quadBL=@(-.5,-.5);uvRect=@(0,0,1,1);sortingLayer=$layer;orderInLayer=$order}}
function B($w,$h,$trigger=$false){@{type='BoxCollider';size=@($w,$h);offset=@(0,0);isTrigger=$trigger;layer='Default'}}
function Body{@{type='Rigidbody2D';velocity=@(0,0);gravity=@(0,0);drag=0;mass=1;restitution=0;friction=0}}
$e=@();$id=1;function E($n,$c,$on=$true){$v=$script:id;$script:id++;@{id=$v;name=$n;enabled=$on;parent=0;components=$c}}
$e+=E 'GameController' @((T 0 0),@{type='Script';script=$g.game;entryFunction='Game'})
$e+=E 'MainCamera' @((T 7.5 8),@{type='Camera';zoom=47;priority=10})
$e+=E 'Sky' @((T 7.5 8 2.5 1.1),(S $g.sky 'Background' -30))
$e+=E 'CloudsA' @((T 5 10 .7 .7),(S $g.clouds 'Background' -20));$e+=E 'CloudsB' @((T 15 10 .7 .7),(S $g.clouds 'Background' -20))
$e+=E 'Ground' @((T 7.5 .25 1.7 .5),(S $g.ground 'Foreground' 20),(B 26 1))
$e+=E 'Ceiling' @((T 7.5 15.7),(B 16 .5))
for($i=1;$i-le3;$i++){foreach($side in 'Top','Bottom'){$y=if($side-eq'Top'){14}else{2};$e+=E "Pipe$i$side" @((T (10+$i*4.2) $y .7 .7),(S $g.tower 'Default' 1),(B 1.3 8))}}
$e+=E 'Glider' @((T 2.5 8 1.4 1.4),(S $g.glider 'Actors' 10),(Body),(B 1.1 .75),@{type='Script';script=$g.game;entryFunction='Bird'})
$e+=E 'HUD' @((T 0 0),@{type='Script';script=$g.hud;entryFunction='SkyHud'})
foreach($prefix in 'ScoreD','BestD'){for($i=0;$i-lt3;$i++){$x=if($prefix-eq'ScoreD'){1+$i*.55}else{12.7+$i*.55};$e+=E "$prefix$i" @((T $x 14.5 1.25 1.25),(S $g.digits 'UI' 100 'digit_0'))}}
Json (Join-Path $scenes 'Game.nscene') @{schema=1;sceneName='Game';nextId=$id;entities=$e};Json (Join-Path $scenes 'Game.nscene.meta') @{schema=1;guid='32000000-0000-4000-8000-999999999999';type='Scene';importSettings=@{}}
Write-Host "Sky Hopper demo created: $Destination"
