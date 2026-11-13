@echo off
chcp 65001 >nul
rem Usage:  合成语音.bat "おはよう、春ちゃん。今日もいい天気だね。"
rem        合成语音.bat "文本" my_output.wav
if "%~1"=="" (
  echo Usage: %~nx0 "Japanese text" [output.wav]
  echo Example: %~nx0 "おはよう、春ちゃん。今日もいい天気だね。"
  pause
  exit /b 1
)
"%~dp0GPT-SoVITS\runtime\python.exe" -X utf8 "%~dp0say.py" %*
pause
