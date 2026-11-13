# ============================================================
#  Build the two GitHub Release assets:
#    release\voice_models_makoto.zip   (~230 MB, voice weights)
#    release\makoto_pet_firmware.zip   (~8 MB, prebuilt firmware)
#
#  Usage:
#    powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1
#
#  This script reads the local model folders next to the repo:
#    ..\makoto\GPT_weights_v2\makoto-e15.ckpt
#    ..\makoto\SoVITS_weights_v2\makoto_e8_s200.pth
#    ..\makoto\output\denoise_opt\vocals.wav_0000040640_0000210560.wav
# ============================================================
param(
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$Root   = Split-Path -Parent $PSScriptRoot
$Parent = Split-Path -Parent $Root
if (-not $OutDir) { $OutDir = Join-Path $Root "release" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$Makoto = Join-Path $Parent "makoto"
$GptW   = Join-Path $Makoto "GPT_weights_v2\makoto-e15.ckpt"
$SovW   = Join-Path $Makoto "SoVITS_weights_v2\makoto_e8_s200.pth"
$RefWav = Join-Path $Makoto "output\denoise_opt\vocals.wav_0000040640_0000210560.wav"

foreach ($p in @($GptW, $SovW, $RefWav)) {
    if (-not (Test-Path $p)) { throw "missing model file: $p" }
}

# ---------- voice pack ----------
Write-Host "[1/2] Packing voice models ..." -ForegroundColor Cyan
$Stage = Join-Path $OutDir "_staging\voice_models\makoto"
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item $GptW   (Join-Path $Stage "makoto-e15.ckpt") -Force
Copy-Item $SovW   (Join-Path $Stage "makoto_e8_s200.pth") -Force
Copy-Item $RefWav (Join-Path $Stage "ref.wav") -Force

$VoiceZip = Join-Path $OutDir "voice_models_makoto.zip"
if (Test-Path $VoiceZip) { Remove-Item $VoiceZip -Force }
Compress-Archive -Path (Join-Path $OutDir "_staging\voice_models") -DestinationPath $VoiceZip -CompressionLevel Optimal
Remove-Item (Join-Path $OutDir "_staging") -Recurse -Force

# ---------- firmware pack ----------
Write-Host "[2/2] Packing firmware ..." -ForegroundColor Cyan
$FwOut = Join-Path $Root "firmware\build_out"
$FwStage = Join-Path $OutDir "_fw"
New-Item -ItemType Directory -Force -Path $FwStage | Out-Null
foreach ($f in @("merged-binary.bin", "bootloader.bin", "partition-table.bin", "makoto_pet.bin")) {
    $src = Join-Path $FwOut $f
    if (-not (Test-Path $src)) { throw "missing firmware file: $src (run firmware\build.ps1 first)" }
    Copy-Item $src $FwStage -Force
}

$FlashDoc = @"
makoto-pet firmware flashing
============================

Option A (recommended): flash the merged image at address 0x0

  esptool.py --chip esp32s3 -p COM15 -b 460800 write_flash 0x0 merged-binary.bin

Option B: flash the three images separately

  esptool.py --chip esp32s3 -p COM15 -b 460800 write_flash `
      0x0     bootloader.bin `
      0x8000  partition-table.bin `
      0x10000 makoto_pet.bin

Replace COM15 with your port. After flashing, the board starts in Wi-Fi
provisioning mode (AP "Makoto-Pet-XXXX", password makoto1234).
"@
Set-Content -Path (Join-Path $FwStage "FLASH.md") -Value $FlashDoc -Encoding ASCII

$FwZip = Join-Path $OutDir "makoto_pet_firmware.zip"
if (Test-Path $FwZip) { Remove-Item $FwZip -Force }
Compress-Archive -Path (Join-Path $FwStage "*") -DestinationPath $FwZip -CompressionLevel Optimal
Remove-Item $FwStage -Recurse -Force

Write-Host ""
Get-Item $VoiceZip, $FwZip | Select-Object Name, @{n="MB";e={[math]::Round($_.Length/1MB,1)}} | Format-Table -AutoSize

Write-Host "Upload both files to a GitHub Release, e.g. tag v1.0.0:" -ForegroundColor Green
Write-Host "  $VoiceZip"
Write-Host "  $FwZip"
Write-Host ""
Write-Host "Then collaborators can run:"
Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -RepoSlug owner/repo"
Write-Host "  (requires GitHub CLI 'gh' and authentication for a private repo)"
