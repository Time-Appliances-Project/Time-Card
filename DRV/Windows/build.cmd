@echo off
rem Build the OCP TimeCard Windows driver and control tool.
setlocal
cd /d "%~dp0"

set "MSBUILD=msbuild"
if defined VSINSTALLDIR if exist "%VSINSTALLDIR%MSBuild\Current\Bin\amd64\MSBuild.exe" set "MSBUILD=%VSINSTALLDIR%MSBuild\Current\Bin\amd64\MSBuild.exe"

where "%MSBUILD%" >nul 2>nul
if errorlevel 1 if not exist "%MSBUILD%" (
    echo error: msbuild not found. Run from a VS developer prompt with WDK.
    exit /b 1
)

echo === Building timecard.sys ===
"%MSBUILD%" timecard.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo /v:m
if errorlevel 1 exit /b 1

echo === Building timecardctl.exe ===
if not exist out mkdir out
cl /nologo /W4 /WX /O2 /I include /Fo:out\ /Fe:out\timecardctl.exe tools\timecardctl.c
if errorlevel 1 exit /b 1

echo.
echo Driver: x64\Release\timecard\
echo Tool:   out\timecardctl.exe
endlocal
