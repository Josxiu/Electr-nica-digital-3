@echo off
rem Doble clic para manejar el CleanBot por Wi-Fi (a traves de la ESP32 puente)
cd /d "%~dp0"
python control_pc.py wifi
pause
