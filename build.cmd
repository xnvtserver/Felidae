@echo off
setlocal

set "CONFIGURATION=debug"
set "PLATFORM=x64"
set "JOBS=1"
set "TEST_ARG="

:parse
if "%~1"=="" goto run
if /i "%~1"=="debug" set "CONFIGURATION=debug"& shift& goto parse
if /i "%~1"=="release" set "CONFIGURATION=release"& shift& goto parse
if /i "%~1"=="sanitize" set "CONFIGURATION=sanitize"& shift& goto parse
if /i "%~1"=="--test" set "TEST_ARG=-Test"& shift& goto parse
if /i "%~1"=="--platform" goto platform
if /i "%~1"=="--jobs" goto jobs
if /i "%~1"=="--help" goto help
if /i "%~1"=="-h" goto help
echo Unknown option: %~1 1>&2
goto help_error

:platform
if "%~2"=="" echo --platform expects x64, x86, arm, or arm64 1>&2& goto help_error
if /i "%~2"=="x64" set "PLATFORM=x64"& shift& shift& goto parse
if /i "%~2"=="x86" set "PLATFORM=x86"& shift& shift& goto parse
if /i "%~2"=="arm" set "PLATFORM=arm"& shift& shift& goto parse
if /i "%~2"=="arm64" set "PLATFORM=arm64"& shift& shift& goto parse
echo Unsupported platform: %~2 1>&2
goto help_error

:jobs
if "%~2"=="" echo --jobs expects a positive integer 1>&2& goto help_error
for /f "delims=0123456789" %%I in ("%~2") do echo --jobs expects a positive integer 1>&2& goto help_error
if "%~2"=="0" echo --jobs expects a positive integer 1>&2& goto help_error
set "JOBS=%~2"
shift
shift
goto parse

:run
if not defined VSINSTALLDIR (
    set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALLDIR=%%I\"
    )
)
if defined VSINSTALLDIR if exist "%VSINSTALLDIR%Common7\Tools\VsDevCmd.bat" (
    call "%VSINSTALLDIR%Common7\Tools\VsDevCmd.bat" -arch=%PLATFORM% -host_arch=x64 >nul
    if errorlevel 1 exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Configuration %CONFIGURATION% -Platform %PLATFORM% -Jobs %JOBS% %TEST_ARG%
exit /b %ERRORLEVEL%

:help
echo usage: build.cmd [debug^|release^|sanitize] [--platform x64^|x86^|arm^|arm64] [--jobs N] [--test]
exit /b 0

:help_error
echo usage: build.cmd [debug^|release^|sanitize] [--platform x64^|x86^|arm^|arm64] [--jobs N] [--test] 1>&2
exit /b 2
