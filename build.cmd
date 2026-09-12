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

set "CONFIGURATION=release"
set "TEST_ARG="

:parse
if "%~1"=="" goto run
if /i "%~1"=="debug" set "CONFIGURATION=debug"& shift& goto parse
if /i "%~1"=="release" set "CONFIGURATION=release"& shift& goto parse
if /i "%~1"=="sanitize" set "CONFIGURATION=sanitize"& shift& goto parse
if /i "%~1"=="--test" set "TEST_ARG=-Test"& shift& goto parse
if /i "%~1"=="--help" goto help
echo Unknown option: %~1 1>&2
goto help_error

:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Configuration %CONFIGURATION% %TEST_ARG%
exit /b %ERRORLEVEL%

:help
echo usage: build.cmd [debug^|release^|sanitize] [--test]
exit /b 0

:help_error
echo usage: build.cmd [debug^|release^|sanitize] [--test] 1>&2
exit /b 2
