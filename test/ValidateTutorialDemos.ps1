param(
    [Parameter(Mandatory=$true)][string]$Player,
    [Parameter(Mandatory=$true)][string]$SourceDir
)
$ErrorActionPreference='Stop'
$runs=@(
    @('platformer','Scenes/Level1.nscene'),
    @('sky-hopper','Scenes/Game.nscene'),
    @('nightfall-arena','Scenes/Arena.nscene'),
    @('ashen-vale','Scenes/Title.nscene'),
    @('ashen-vale','Scenes/Town.nscene'),
    @('ashen-vale','Scenes/Dungeon.nscene'),
    @('turbo-trails','Scenes/Race.nscene')
)
foreach($run in $runs){
    $project=Join-Path $SourceDir ("samples/{0}/DemoProject" -f $run[0])
    if(-not(Test-Path -LiteralPath $project -PathType Container)){
        throw "Missing tutorial DemoProject: $project"
    }
    $global:LASTEXITCODE=0
    $output=&$Player --project $project --scene $run[1] --frames 30 2>&1|Out-String
    if($LASTEXITCODE-ne0-or$output-match'\[lua-error\]|\[ERROR\]'){
        throw ("Tutorial demo failed: {0} {1}`n{2}" -f $run[0],$run[1],$output)
    }
    Write-Host ("[PASS] {0} {1}" -f $run[0],$run[1])
}
