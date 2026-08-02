@echo off
rem Build the x64 OCP Time Card W32Time input provider.
setlocal
cd /d "%~dp0"

set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
)
set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo error: the Visual Studio x64 C++ build environment was not found.
    exit /b 1
)
set "OUT=TimeCardTimeProvider\bin\Release"
if not exist "%OUT%" mkdir "%OUT%"

call "%VCVARS%" >nul
rc.exe /nologo /fo "%OUT%\timecard_time_provider.res" ^
  TimeCardTimeProvider\timecard_time_provider.rc
if errorlevel 1 exit /b 1
cl.exe /nologo /LD /O2 /MT /W4 /WX /DUNICODE /D_UNICODE ^
  /Fo:"%OUT%\timecard_time_provider.obj" ^
  TimeCardTimeProvider\timecard_time_provider.c ^
  "%OUT%\timecard_time_provider.res" ^
  /link /DEF:TimeCardTimeProvider\timecard_time_provider.def ^
  /OUT:"%OUT%\TimeCardTimeProvider.dll" ^
  /IMPLIB:"%OUT%\TimeCardTimeProvider.lib"
if errorlevel 1 exit /b 1
if not exist "%OUT%\TimeCardTimeProvider.dll" (
    echo error: the W32Time provider DLL was not created.
    exit /b 1
)
echo Provider: %OUT%\TimeCardTimeProvider.dll
endlocal
