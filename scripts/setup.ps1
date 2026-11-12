# ============================================================
#  makoto-pet setup for Windows (PowerShell 5.1+)
#
#  Usage:
#    powershell -ExecutionPolicy Bypass -File scripts\setup.ps1
#    powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -VoicePackZip D:\voice_models_makoto.zip
#    powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -RepoSlug owner/repo
#
#  What it does:
#    1. checks Python 3.10+
#    2. creates server\.venv and installs dependencies
#    3. copies server\config.example.yaml to server\config.yaml (if missing)
#    4. extracts the voice pack to ..\voice_models\makoto
#
#  It does NOT install GPT-SoVITS (about 12 GB). See README.
# ============================================================
param(
    [string]$VoicePackZip = "",
    [string]$VoicePackUrl = "",
    [string]$GitHubToken = "",
    [string]$RepoSlug = ""
)

$ErrorActionPreference = "Stop"

$Root    = Split-Path -Parent $PSScriptRoot
$Server  = Join-Path $Root "server"
$Parent  = Split-Path -Parent $Root
$Venv    = Join-Path $Server ".venv"
$Py      = Join-Path $Venv "Scripts\python.exe"
$VoiceDir = Join-Path $Parent "voice_models\makoto"

function Extract-VoicePack([string]$zipPath) {
    $tmp = Join-Path $env:TEMP ("makoto_voice_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    Expand-Archive -LiteralPath $zipPath -DestinationPath $tmp -Force
    $src = Join-Path $tmp "voice_models\makoto"
    if (-not (Test-Path $src)) { $src = Join-Path $tmp "makoto" }
    if (-not (Test-Path $src)) { $src = $tmp }
    New-Item -ItemType Directory -Force -Path $VoiceDir | Out-Null
    Copy-Item (Join-Path $src "*") $VoiceDir -Force -Recurse
    Remove-Item $tmp -Recurse -Force
}

Write-Host "[1/4] Checking Python 3.10+ ..." -ForegroundColor Cyan
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "python not found in PATH. Install Python 3.10+ and enable 'Add to PATH'."
}
& python -c "import sys; raise SystemExit(0 if sys.version_info >= (3,10) else 1)"
if ($LASTEXITCODE -ne 0) { throw "Python 3.10+ is required." }

Write-Host "[2/4] Creating venv and installing dependencies ..." -ForegroundColor Cyan
if (-not (Test-Path $Py)) {
    & python -m venv $Venv
    if ($LASTEXITCODE -ne 0) { throw "python -m venv failed." }
}
& $Py -m pip install --upgrade pip
$Lock = Join-Path $Server "requirements-lock.txt"
$Req  = if (Test-Path $Lock) { $Lock } else { Join-Path $Server "requirements.txt" }
& $Py -m pip install -r $Req
if ($LASTEXITCODE -ne 0) { throw "pip install failed." }

Write-Host "[3/4] Preparing server\config.yaml ..." -ForegroundColor Cyan
$Cfg  = Join-Path $Server "config.yaml"
$Tmpl = Join-Path $Server "config.example.yaml"
if (-not (Test-Path $Cfg)) {
    Copy-Item $Tmpl $Cfg
    Write-Host "  created server\config.yaml"
}
Write-Host "  fill llm.api_key in server\config.yaml, or set MAKOTO_LLM_KEY."

Write-Host "[4/4] Voice pack ..." -ForegroundColor Cyan
if (Test-Path (Join-Path $VoiceDir "makoto-e15.ckpt")) {
    Write-Host "  voice pack already present: $VoiceDir"
}
elseif ($VoicePackZip) {
    if (-not (Test-Path $VoicePackZip)) { throw "voice pack zip not found: $VoicePackZip" }
    Extract-VoicePack (Resolve-Path $VoicePackZip).Path
    Write-Host "  extracted to $VoiceDir"
}
elseif ($RepoSlug -and (Get-Command gh -ErrorAction SilentlyContinue)) {
    $tmp = Join-Path $env:TEMP ("makoto_voice_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    gh release download --repo $RepoSlug --pattern "voice_models_makoto.zip" --dir $tmp --clobber
    if ($LASTEXITCODE -ne 0) { throw "gh release download failed. Is gh authenticated?" }
    Extract-VoicePack (Join-Path $tmp "voice_models_makoto.zip")
    Remove-Item $tmp -Recurse -Force
    Write-Host "  extracted to $VoiceDir"
}
elseif ($VoicePackUrl) {
    $zip = Join-Path $env:TEMP ("makoto_voice_" + [guid]::NewGuid().ToString("N") + ".zip")
    $headers = @{}
    if ($GitHubToken) { $headers["Authorization"] = "Bearer $GitHubToken" }
    Invoke-WebRequest -Uri $VoicePackUrl -OutFile $zip -Headers $headers -UseBasicParsing
    Extract-VoicePack $zip
    Remove-Item $zip -Force
    Write-Host "  extracted to $VoiceDir"
}
else {
    Write-Host "  voice pack not found." -ForegroundColor Yellow
    Write-Host "  Download voice_models_makoto.zip from the GitHub Release, then run:"
    Write-Host "    powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -VoicePackZip <path>"
    Write-Host "  Private repo alternative (gh CLI authenticated):"
    Write-Host "    powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -RepoSlug owner/repo"
}

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "Next steps:"
Write-Host "  1. Install GPT-SoVITS next to this repo (folder name GPT-SoVITS),"
Write-Host "     or set GPT_SOVITS_DIR to its location."
Write-Host "  2. Double-click server\start_gptsovits.bat, wait for port 9880."
Write-Host "  3. Double-click server\run.bat, open http://127.0.0.1:8765"
