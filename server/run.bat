@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

set "NO_PROXY=localhost,127.0.0.1,::1"
set "no_proxy=localhost,127.0.0.1,::1"
set "HF_ENDPOINT=https://hf-mirror.com"
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

if exist ".venv\Scripts\python.exe" (
  ".venv\Scripts\python.exe" -c "import sys" >nul 2>nul
  if errorlevel 1 (
    echo [1/3] Existing .venv does not work on this machine, recreating...
    rmdir /s /q ".venv"
  )
)

if not exist ".venv\Scripts\python.exe" (
  echo [1/3] Creating Python venv...
  python -m venv .venv
  if errorlevel 1 goto :err
)

set "REQ=requirements.txt"
if exist "requirements-lock.txt" set "REQ=requirements-lock.txt"

echo [2/3] Checking dependencies...
".venv\Scripts\python.exe" -c "import fastapi, uvicorn, httpx, yaml, numpy, multipart, faster_whisper, bs4, trafilatura, ddgs, pypdf, docx, pymupdf, opencc, pypinyin, wordfreq, jieba" >nul 2>nul
if errorlevel 1 (
  echo     Installing dependencies. First run can take several minutes...
  ".venv\Scripts\python.exe" -m pip install --disable-pip-version-check -r %REQ%
  if errorlevel 1 (
    echo     Lock file failed, retrying with requirements.txt...
    ".venv\Scripts\python.exe" -m pip install --disable-pip-version-check -r requirements.txt
    if errorlevel 1 goto :err
  )
)

echo [3/3] Starting Makoto Brain...
echo     Web console : http://127.0.0.1:8765
echo     Device WS   : ws://YOUR-PC-IP:8765/ws/device
echo.
".venv\Scripts\python.exe" -m uvicorn app.main:app --host 0.0.0.0 --port 8765
echo.
echo Server exited with code %errorlevel%.
goto :end

:err
echo.
echo Dependency install failed. Check Python 3.10+ is installed and python is in PATH.

:end
pause
