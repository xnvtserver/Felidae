@echo off
setlocal
if "%~1"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
) else (
    if "%~1"=="--target" (
        powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Target %2
    ) else (
        powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Target %1
    )
)
exit /b %ERRORLEVEL%
