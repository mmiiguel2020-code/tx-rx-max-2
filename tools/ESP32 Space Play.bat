@echo off
title ESP32 RX CC88 hold=Play release=Stop
cd /d "%~dp0"
echo Cierra esta ventana solo con Ctrl+C
echo Si FL tiene el MIDI del ESP32, dejalo; el puente envia Space a la ventana FL.
py -3.12 "%~dp0esp32_space_play.py"
if errorlevel 1 pause
