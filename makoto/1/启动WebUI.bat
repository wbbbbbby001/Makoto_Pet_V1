@echo off
chcp 65001 >nul
rem --- 本机装了系统代理(127.0.0.1:7897)，必须让 localhost 绕过代理，
rem     否则 Gradio 启动自检 localhost 会失败并直接退出 ---
set NO_PROXY=localhost,127.0.0.1,::1
set no_proxy=localhost,127.0.0.1,::1
cd /d "%~dp0GPT-SoVITS"
echo ============================================================
echo   GPT-SoVITS  WebUI   (Makoto Tachibana)
echo.
echo   主界面   http://127.0.0.1:9874
echo   推理页   http://127.0.0.1:9872   (在主界面点「推理」会开这个)
echo ============================================================
"%~dp0GPT-SoVITS\runtime\python.exe" webui.py
pause
