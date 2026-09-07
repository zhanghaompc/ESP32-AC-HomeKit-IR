@echo off
setlocal
cd /d "%~dp0"
set "PLATFORMIO_CORE_DIR=%USERPROFILE%\.platformio"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
"%PIO%" run -e esp32_wifi %*
endlocal
