param(
    [Parameter(Mandatory = $true)]
    [string]$Player,
    [string]$SourceDir = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$utf8 = [System.Text.UTF8Encoding]::new($false)

function Get-LuaFences([string]$Path) {
    $lines = [IO.File]::ReadAllLines($Path, [Text.Encoding]::UTF8)
    $blocks = [Collections.Generic.List[string]]::new()
    $inside = $false
    $current = [Collections.Generic.List[string]]::new()
    foreach ($line in $lines) {
        if (-not $inside -and $line -eq '```lua') {
            $inside = $true
            $current = [Collections.Generic.List[string]]::new()
        } elseif ($inside -and $line -eq '```') {
            $blocks.Add(($current -join "`n"))
            $inside = $false
        } elseif ($inside) {
            $current.Add($line)
        }
    }
    return $blocks
}

function Get-NamedBlocks([string]$Path) {
    return @(Get-LuaFences $Path | Where-Object { $_ -match '(?m)^-- Assets/Scripts/' })
}

function Invoke-LuaSmoke([string]$Name, [string[]]$Scripts) {
    $root = Join-Path ([IO.Path]::GetTempPath()) ("naragame-tutorial-" + $Name)
    $expectedPrefix = Join-Path ([IO.Path]::GetTempPath()) 'naragame-tutorial-'
    if (-not ([IO.Path]::GetFullPath($root)).StartsWith([IO.Path]::GetFullPath($expectedPrefix))) {
        throw "Unsafe tutorial smoke directory: $root"
    }
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
    $scriptsDir = Join-Path $root 'Assets/Scripts'
    $scenesDir = Join-Path $root 'Assets/Scenes'
    $settingsDir = Join-Path $root 'ProjectSettings'
    New-Item -ItemType Directory -Force -Path $scriptsDir,$scenesDir,$settingsDir | Out-Null

    $entities = [Collections.Generic.List[object]]::new()
    $id = 1
    foreach ($script in $Scripts) {
        $guid = "20000000-0000-4000-8000-{0:D12}" -f $id
        $scriptName = "Smoke$id.lua"
        [IO.File]::WriteAllText((Join-Path $scriptsDir $scriptName), $script, $utf8)
        $meta = @{ schema = 1; guid = $guid; type = 'Script'; importSettings = @{} } |
            ConvertTo-Json -Compress
        [IO.File]::WriteAllText((Join-Path $scriptsDir ($scriptName + '.meta')), $meta, $utf8)
        $entities.Add(@{
            id = $id; parent = 0; name = "Script$id"; enabled = $true
            components = @(
                @{ type = 'Transform'; position = @(0,0,0); rotation = 0; scale = @(1,1,1) },
                @{ type = 'Script'; script = $guid; entryFunction = 'CompileOnly' }
            )
        })
        $id++
    }

    $scene = @{ schema = 1; sceneName = 'Smoke'; nextId = $id; entities = $entities } |
        ConvertTo-Json -Depth 12 -Compress
    [IO.File]::WriteAllText((Join-Path $scenesDir 'Smoke.nscene'), $scene, $utf8)
    [IO.File]::WriteAllText((Join-Path $scenesDir 'Smoke.nscene.meta'),
        '{"schema":1,"guid":"20000000-0000-4000-8000-999999999999","type":"Scene","importSettings":{}}', $utf8)
    [IO.File]::WriteAllText((Join-Path $settingsDir 'project.json'),
        ('{{"schema":1,"name":"{0}","version":"0.1.0","mainScene":"Scenes/Smoke.nscene","sortingLayers":["Default"]}}' -f $Name), $utf8)

    $global:LASTEXITCODE = 0
    $output = & $Player --project $root --frames 2 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $output -match '(?i)lua.*(error|failed)|syntax error') {
        throw "$Name tutorial Lua failed to load:`n$output"
    }
    Write-Host ("[PASS] {0}: {1} Lua chunk(s) loaded" -f $Name, $Scripts.Count)
}

$sampleRoot = Join-Path $SourceDir 'samples'

$platformer = Get-NamedBlocks (Join-Path $sampleRoot 'platformer/README.md')
Invoke-LuaSmoke 'platformer' $platformer

$skyBlocks = Get-LuaFences (Join-Path $sampleRoot 'sky-hopper/README.md')
Invoke-LuaSmoke 'sky-hopper' @($skyBlocks[0], $skyBlocks[1])

$arenaBlocks = Get-LuaFences (Join-Path $sampleRoot 'nightfall-arena/README.md')
Invoke-LuaSmoke 'nightfall-arena' @($arenaBlocks[0])

$ashenBlocks = Get-LuaFences (Join-Path $sampleRoot 'ashen-vale/README.md')
# Blocks 1 through 19 are the consecutive AshenVale.lua implementation.
$ashenScript = ($ashenBlocks[1..19] -join "`n`n")
Invoke-LuaSmoke 'ashen-vale' @($ashenScript)

Write-Host 'All tutorial Lua runtime smoke checks passed.'
