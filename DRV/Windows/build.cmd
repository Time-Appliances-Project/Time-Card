@echo off
rem Build the OCP TimeCard Windows driver and control tool.
setlocal
cd /d "%~dp0"

set "SIGNMODE=Off"
if /i "%~1"=="test" set "SIGNMODE=TestSign"
if /i "%~1"=="release" set "SIGNMODE=Off"
if not "%~1"=="" if /i not "%~1"=="test" if /i not "%~1"=="release" (
    echo usage: build.cmd [release^|test]
    exit /b 2
)

set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
)
set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo error: the Visual Studio x64 C++ build environment was not found.
    exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

set "MSBUILD=msbuild.exe"
if exist "%VSROOT%\MSBuild\Current\Bin\amd64\MSBuild.exe" set "MSBUILD=%VSROOT%\MSBuild\Current\Bin\amd64\MSBuild.exe"

where "%MSBUILD%" >nul 2>nul
if errorlevel 1 if not exist "%MSBUILD%" (
    echo error: msbuild not found. Run from a VS developer prompt with WDK.
    exit /b 1
)

echo === Building timecard.sys ^(%SIGNMODE%^) ===
"%MSBUILD%" timecard.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SignMode=%SIGNMODE% /nologo /v:m
if errorlevel 1 exit /b 1

echo === Building timecardctl.exe ===
if not exist out mkdir out
cl /nologo /W4 /WX /O2 /I include /Fo:out\ /Fe:out\timecardctl.exe tools\timecardctl.c
if errorlevel 1 exit /b 1

echo.
echo Driver: x64\Release\timecard\
echo Tool:   out\timecardctl.exe
endlocal
