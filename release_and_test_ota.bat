@echo off
chcp 65001 >nul
cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0release_and_test_ota.ps1" %*
if errorlevel 1 goto failed

echo.
echo Release and OTA test completed.
goto done

:failed
echo.
echo Release failed. See output above.

:done
pause
endlocal
