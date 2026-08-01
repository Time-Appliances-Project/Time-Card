@echo off
rem Build the dependency-free OCP Time Card Control Center desktop application.
setlocal
cd /d "%~dp0"

set "CONFIG=Release"
if /i "%~1"=="debug" set "CONFIG=Debug"
if /i "%~1"=="release" set "CONFIG=Release"
if not "%~1"=="" if /i not "%~1"=="debug" if /i not "%~1"=="release" (
    echo usage: build-gui.cmd [release^|debug]
    exit /b 2
)

set "MSBUILD=msbuild.exe"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" (
    set "MSBUILD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\amd64\MSBuild.exe`) do set "MSBUILD=%%i"
)

echo === Building OCP Time Card Control Center ^(%CONFIG%^) ===
call build-discipline-library.cmd
if errorlevel 1 exit /b 1
"%MSBUILD%" TimeCardControlCenter\TimeCardControlCenter.csproj /t:Rebuild /p:Configuration=%CONFIG% /p:Platform=AnyCPU /nologo /v:minimal
if errorlevel 1 exit /b 1

echo.
echo Application: TimeCardControlCenter\bin\%CONFIG%\TimeCardControlCenter.exe
