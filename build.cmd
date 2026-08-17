@echo off
setlocal
if not defined VSINSTALLDIR (
    set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALLDIR=%%I\"
    )
)
if defined VSINSTALLDIR if exist "%VSINSTALLDIR%Common7\Tools\VsDevCmd.bat" (
    call "%VSINSTALLDIR%Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
)
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
if "%~1"=="--dist" (
    set "PS_ARGS=%PS_ARGS% -Dist"
    shift
    goto parse
)
if "%~1"=="--beta" (
    set "PS_ARGS=%PS_ARGS% -Beta"
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
if "%~1"=="-Dist" (
    set "PS_ARGS=%PS_ARGS% -Dist"
    shift
    goto parse
)
if "%~1"=="-Beta" (
    set "PS_ARGS=%PS_ARGS% -Beta"
    shift
    goto parse
)
set "PS_ARGS=%PS_ARGS% -Target %~1"
shift
goto parse

:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %PS_ARGS%
exit /b %ERRORLEVEL%
