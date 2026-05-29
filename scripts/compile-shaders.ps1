# Compiles GLSL shaders in engine/shaders to SPIR-V and embeds the bytes into
# C++ headers under engine/src/renderer. Run when a .vert/.frag source changes.
# The generated headers are committed so builds don't depend on Vulkan SDK.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$glslc = if ($env:VULKAN_SDK) { Join-Path $env:VULKAN_SDK "Bin\glslc.exe" } else { "glslc.exe" }
if (-not (Test-Path $glslc)) { throw "glslc not found ($glslc) -- install Vulkan SDK or set VULKAN_SDK." }

function Emit-Spv($vert, $frag, $headerPath, $vertSymbol, $fragSymbol) {
    $tmp = "$env:TEMP\nara_spv"
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    $vSpv = "$tmp\$(Split-Path -Leaf $vert).spv"
    $fSpv = "$tmp\$(Split-Path -Leaf $frag).spv"
    & $glslc -fshader-stage=vert $vert -o $vSpv
    if ($LASTEXITCODE -ne 0) { throw "glslc failed on $vert" }
    & $glslc -fshader-stage=frag $frag -o $fSpv
    if ($LASTEXITCODE -ne 0) { throw "glslc failed on $frag" }

    function Bytes-To-CArray($bytes, $name) {
        $sb = New-Object System.Text.StringBuilder
        [void]$sb.AppendLine("inline const uint32_t ${name}[] = {")
        for ($i = 0; $i -lt $bytes.Length; $i += 4) {
            $u = [BitConverter]::ToUInt32($bytes, $i)
            $hex = "0x{0:x8}u" -f $u
            if (($i / 4) % 8 -eq 0) { [void]$sb.Append("    ") }
            [void]$sb.Append($hex)
            if ($i + 4 -lt $bytes.Length) { [void]$sb.Append(", ") }
            if ((($i / 4) % 8) -eq 7) { [void]$sb.AppendLine() }
        }
        [void]$sb.AppendLine()
        [void]$sb.AppendLine("};")
        return $sb.ToString()
    }

    $vBytes = [IO.File]::ReadAllBytes($vSpv)
    $fBytes = [IO.File]::ReadAllBytes($fSpv)

    $hdr = New-Object System.Text.StringBuilder
    [void]$hdr.AppendLine("#pragma once")
    [void]$hdr.AppendLine("// Auto-generated from engine/shaders by scripts/compile-shaders.ps1. Do not edit.")
    [void]$hdr.AppendLine("#include <cstdint>")
    [void]$hdr.AppendLine()
    [void]$hdr.AppendLine("namespace NaraGame {")
    [void]$hdr.Append((Bytes-To-CArray $vBytes $vertSymbol))
    [void]$hdr.AppendLine()
    [void]$hdr.Append((Bytes-To-CArray $fBytes $fragSymbol))
    [void]$hdr.AppendLine("} // namespace NaraGame")

    [IO.File]::WriteAllText($headerPath, $hdr.ToString(), (New-Object System.Text.UTF8Encoding $false))
    "wrote $headerPath"
}

Emit-Spv `
    -vert "$root\engine\shaders\msdf.vert" `
    -frag "$root\engine\shaders\msdf.frag" `
    -headerPath "$root\engine\src\renderer\MsdfSpirv.h" `
    -vertSymbol "kMsdfVertSpv" `
    -fragSymbol "kMsdfFragSpv"
