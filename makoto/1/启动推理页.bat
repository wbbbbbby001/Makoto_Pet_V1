@echo off
chcp 65001 >nul
rem 只开推理页（TTS 界面），不启动主界面。同样必须让 localhost 绕过系统代理。
set NO_PROXY=localhost,127.0.0.1,::1
set no_proxy=localhost,127.0.0.1,::1
cd /d "%~dp0GPT-SoVITS"
echo ============================================================
echo   GPT-SoVITS 推理页 (Makoto Tachibana)
echo   http://127.0.0.1:9872
echo.
echo   参考音频: %~dp0output\denoise_opt\
echo   参考文本: %~dp0logs\makoto\2-name2text.txt
echo ============================================================
"%~dp0GPT-SoVITS\runtime\python.exe" GPT_SoVITS\inference_webui.py
pause
