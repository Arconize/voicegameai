# package.ps1
$ErrorActionPreference = "Stop"

$release = "build\Release"
$game    = "$release\horror_voice_npc.exe"
$packer  = "$release\packer.exe"
$output  = "$release\HorrorVoiceNPC-Standalone.exe"

if (!(Test-Path $game))   { Write-Error "Game exe not found: $game" }
if (!(Test-Path $packer)) { Write-Error "Packer not found: $packer" }

$files = @(
    "$release\whisper.dll:whisper.dll",
    "$release\llama.dll:llama.dll",
    "$release\ggml.dll:ggml.dll",
    "$release\ggml-base.dll:ggml-base.dll",
    "$release\ggml-cpu.dll:ggml-cpu.dll",
    "$release\parakeet.dll:parakeet.dll",

    "models\TypoRPV3-2B.Q4_K_M.gguf:models/TypoRPV3-2B.Q4_K_M.gguf",
    "models\ggml-small.en.bin:models/ggml-small.en.bin"
)

Write-Host "Packing into $output ..."
& $packer $game $output @files
Write-Host "Done."
