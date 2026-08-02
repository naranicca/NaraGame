param([string]$Destination=(Join-Path $PSScriptRoot 'DemoProject'))
$ErrorActionPreference='Stop';$utf8=[Text.UTF8Encoding]::new($false)
$a=Join-Path $Destination 'Assets';$sc=Join-Path $a 'Scenes';$ss=Join-Path $a 'Scripts';$set=Join-Path $Destination 'ProjectSettings'
[IO.Directory]::CreateDirectory($sc)|Out-Null;[IO.Directory]::CreateDirectory($ss)|Out-Null;[IO.Directory]::CreateDirectory($set)|Out-Null
Copy-Item (Join-Path $PSScriptRoot 'Assets/Sprites') $a -Recurse -Force;Copy-Item (Join-Path $PSScriptRoot 'Assets/Sounds') $a -Recurse -Force
function J($p,$v){[IO.File]::WriteAllText($p,($v|ConvertTo-Json -Depth 30 -Compress),$utf8)}
$read=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'README.md'),[Text.Encoding]::UTF8);$m=[regex]::Match($read,'(?s)```lua\s*(-- Assets/Scripts/ArenaGame.lua.*?)\s*```');if(!$m.Success){throw 'ArenaGame tutorial block not found'}
[IO.File]::WriteAllText((Join-Path $ss 'ArenaGame.lua'),$m.Groups[1].Value,$utf8)
$g=@{script='33000000-0000-4000-8000-000000000001';player='33000000-0000-4000-8000-000000000002';enemy='33000000-0000-4000-8000-000000000003';bullet='33000000-0000-4000-8000-000000000004';gem='33000000-0000-4000-8000-000000000005';floor='33000000-0000-4000-8000-000000000006';digits='33000000-0000-4000-8000-000000000007'}
J (Join-Path $ss 'ArenaGame.lua.meta') @{schema=1;guid=$g.script;type='Script';importSettings=@{}}
function Slice($n,$x){@{name=$n;rect=@($x,0,16,24);pivot=@(.5,.5);border=@(0,0,0,0)}}
function Tex($n,$guid,$ppu,$s=$null){$i=@{pixelsPerUnit=$ppu;pivotMode='Center';customPivot=@(.5,.5);filterMode='Point';wrapMode='Clamp';alphaIsTransparency=$true;border=@(0,0,0,0);spriteMode=if($s){'Multiple'}else{'Single'}};if($s){$i.spriteSheet=@{sprites=$s}};J ((Join-Path $a "Sprites/$n")+'.meta') @{schema=1;guid=$guid;type='Texture';importSettings=$i}}
Tex 'ranger.png' $g.player 32;Tex 'shade.png' $g.enemy 32;Tex 'spark.png' $g.bullet 32;Tex 'gem.png' $g.gem 32;Tex 'arena_floor.png' $g.floor 32
$ds=@();for($i=0;$i-lt10;$i++){$ds+=Slice "digit_$i" ($i*16)};Tex 'digits.png' $g.digits 32 $ds
J (Join-Path $set 'project.json') @{schema=1;name='NightfallArenaDemo';version='0.1.0';mainScene='Scenes/Arena.nscene';sortingLayers=@('Background','Default','Actors','Effects','UI')}
function T($x,$y,$sx=1,$sy=1){@{type='Transform';position=@($x,$y,0);rotation=0;scale=@($sx,$sy,1)}}
function S($guid,$layer='Default',$order=0,$sub='',$color=@(1,1,1,1)){@{type='SpriteRenderer';sprite=$guid;subSprite=$sub;color=$color;flipX=$false;flipY=$true;drawMode='Simple';size=@(1,1);customQuad=$false;quadTL=@(-.5,.5);quadTR=@(.5,.5);quadBR=@(.5,-.5);quadBL=@(-.5,-.5);uvRect=@(0,0,1,1);sortingLayer=$layer;orderInLayer=$order}}
function RectSprite($guid,$w,$h,$color,$order){$s=S $guid 'UI' $order '' $color;$s.size=@($w,$h);$s}
$e=@();$id=1;function E($n,$c,$on=$true,$parent=0){$v=$script:id;$script:id++;@{id=$v;name=$n;enabled=$on;parent=$parent;components=$c}}
$e+=E 'ArenaController' @((T 0 0),@{type='Script';script=$g.script;entryFunction='Arena'})
$e+=E 'MainCamera' @((T 0 0),@{type='Camera';zoom=48;priority=10})
$e+=E 'ArenaFloor' @((T 0 0 3.4 3.4),(S $g.floor 'Background' -20))
$e+=E 'Player' @((T 0 0 1.15 1.15),(S $g.player 'Actors' 10))
for($i=1;$i-le30;$i++){$e+=E "Enemy$i" @((T 0 0),(S $g.enemy 'Actors' 8)) $false}
for($i=1;$i-le24;$i++){$e+=E "Bullet$i" @((T 0 0 .75 .75),(S $g.bullet 'Effects' 20)) $false}
for($i=1;$i-le30;$i++){$e+=E "Gem$i" @((T 0 0 .8 .8),(S $g.gem 'Effects' 15)) $false}
$e+=E 'HpBack' @((T -5.5 6.5),(RectSprite $g.floor 3.4 .28 @(.12,.08,.12,1) 90)) $true 2
$e+=E 'HpFill' @((T -5.5 6.5),(RectSprite $g.floor 3.2 .24 @(1,.15,.2,1) 91)) $true 2
$e+=E 'XpBack' @((T -5.5 6.05),(RectSprite $g.floor 3.4 .2 @(.08,.1,.14,1) 90)) $true 2
$e+=E 'XpFill' @((T -5.5 6.05),(RectSprite $g.floor 3.2 .16 @(.1,.9,.85,1) 91)) $true 2
foreach($p in 'LevelD','KillD'){ $count=if($p-eq'LevelD'){2}else{3};for($i=0;$i-lt$count;$i++){$x=if($p-eq'LevelD'){-0.3+$i*.55}else{5+$i*.55};$e+=E "$p$i" @((T $x 6.3 1.25 1.25),(S $g.digits 'UI' 100 'digit_0')) $true 2}}
$e+=E 'GameOverPanel' @((T 0 0),(RectSprite $g.floor 6 3 @(.35,.03,.08,.9) 200)) $false 2
J (Join-Path $sc 'Arena.nscene') @{schema=1;sceneName='Arena';nextId=$id;entities=$e};J (Join-Path $sc 'Arena.nscene.meta') @{schema=1;guid='33000000-0000-4000-8000-999999999999';type='Scene';importSettings=@{}}
Write-Host "Nightfall Arena demo created: $Destination"
