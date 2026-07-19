@echo off
setlocal
set "PS_ARGS="
:parse
if "%~1"=="" goto run
if "%~1"=="--target" (
    set "PS_ARGS=%PS_ARGS% -Target %~2"
    shift
    shift
    goto parse
)
if "%~1"=="--configuration" (
    set "PS_ARGS=%PS_ARGS% -Configuration %~2"
    shift
    shift
    goto parse
)
if "%~1"=="--warnings-as-errors" (
    set "PS_ARGS=%PS_ARGS% -WarningsAsErrors"
    shift
    goto parse
)
if "%~1"=="-Target" (
    set "PS_ARGS=%PS_ARGS% -Target %~2"
    shift
    shift
    goto parse
)
if "%~1"=="-Configuration" (
    set "PS_ARGS=%PS_ARGS% -Configuration %~2"
    shift
    shift
    goto parse
)
if "%~1"=="-WarningsAsErrors" (
    set "PS_ARGS=%PS_ARGS% -WarningsAsErrors"
    shift
    goto parse
)
set "PS_ARGS=%PS_ARGS% -Target %~1"
shift
goto parse

:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %PS_ARGS%
exit /b %ERRORLEVEL%
