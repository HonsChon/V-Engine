# Injects register bindings into spirv-cross HLSL output for DX12.
#
# spirv-cross --hlsl --shader-model 60 already maps descriptor sets to register
# spaces (set N -> space N, binding B -> register B). The only gap is push
# constant blocks: spirv-cross emits them as plain "cbuffer Name {...}" without
# any register, so they would collide with descriptor-space cbuffers.
#
# Convention (docs/DX12-RHI-Notes.md section 11 appendix):
#   push constants live at register b0 in a dedicated space equal to the number
#   of binding layouts of the pipeline.
#
# Usage:
#   powershell -File inject_hlsl.ps1 -InFile in.hlsl -OutFile out.hlsl -PushConstantSpace 2
#
# Any "cbuffer X" declaration that has no ": register" is treated as a push
# constant block and gets " : register(b0, space<N>)" appended.

param(
    [Parameter(Mandatory = $true)][string]$InFile,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [Parameter(Mandatory = $true)][int]$PushConstantSpace
)

$text = [System.IO.File]::ReadAllText($InFile)

# Match "cbuffer Name" at end of line (no register suffix yet), immediately
# followed by '{' on the next line.
$pattern = '(?m)^(cbuffer [A-Za-z_][A-Za-z0-9_]*)(\s*)$(?=\r?\n\{)'
$replacement = "`$1 : register(b0, space$PushConstantSpace)"
$updated = [regex]::Replace($text, $pattern, $replacement)

[System.IO.File]::WriteAllText($OutFile, $updated)
Write-Host "[inject_hlsl] $InFile -> $OutFile (push constants in space $PushConstantSpace)"
