@echo off
chcp 65001 >nul
setlocal

rem Locate GPT-SoVITS: GPT_SOVITS_DIR first, then ..\..\GPT-SoVITS,
rem then the author's local layout ..\..\makoto\GPT-SoVITS.
set "GPS=%GPT_SOVITS_DIR%"
if not defined GPS if exist "%~dp0..\..\GPT-SoVITS\runtime\python.exe" set "GPS=%~dp0..\..\GPT-SoVITS"
if not defined GPS if exist "%~dp0..\..\makoto\GPT-SoVITS\runtime\python.exe" set "GPS=%~dp0..\..\makoto\GPT-SoVITS"
if not defined GPS set "GPS=%~dp0..\..\GPT-SoVITS"

cd /d "%GPS%"

if not exist "runtime\python.exe" (
  echo GPT-SoVITS runtime not found:
  echo   %CD%
  echo.
  echo Install GPT-SoVITS first, then either:
  echo   1. put it next to this repo as "GPT-SoVITS", or
  echo   2. set GPT_SOVITS_DIR to its folder.
  pause
  exit /b 1
)

rem GPT-SoVITS loads reference audio through ffmpeg by name.
rem Its folder ships ffmpeg.exe / ffprobe.exe, so put it on PATH.
set "PATH=%CD%;%PATH%"
set "NO_PROXY=localhost,127.0.0.1,::1"
set "no_proxy=localhost,127.0.0.1,::1"
echo Starting GPT-SoVITS API on http://127.0.0.1:9880
runtime\python.exe api_v2.py -a 127.0.0.1 -p 9880 -c GPT_SoVITS/configs/tts_infer.yaml
echo.
echo GPT-SoVITS exited with code %errorlevel%.
pause
