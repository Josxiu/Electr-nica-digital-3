@echo off
rem Doble clic para manejar el CleanBot con las flechas (Thonny debe estar cerrado)
cd /d "%~dp0"
python control_pc.py COM8
pause
