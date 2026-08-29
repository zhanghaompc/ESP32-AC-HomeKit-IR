@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\release.ps1" %*
if errorlevel 1 goto failed
echo.
echo Release completed.
goto done
:failed
echo.
echo Release failed. See the error above.
:done
pause
endlocal
