@echo off
rem Build the native OCP Time Card oscillatord Windows Service.
setlocal
cd /d "%~dp0"

set "CONFIG=Release"
if /i "%~1"=="debug" set "CONFIG=Debug"
if /i "%~1"=="release" set "CONFIG=Release"
if not "%~1"=="" if /i not "%~1"=="debug" if /i not "%~1"=="release" (
    echo usage: build-oscillatord-service.cmd [release^|debug]
    exit /b 2
)

set "PROJECT=TimeCardOscillatord\TimeCardOscillatord.csproj"
if not exist "%PROJECT%" (
    echo error: service project not found: %PROJECT%
    exit /b 1
)

set "MSBUILD=msbuild.exe"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" (
    set "MSBUILD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\amd64\MSBuild.exe`) do set "MSBUILD=%%i"
)

where "%MSBUILD%" >nul 2>nul
if errorlevel 1 if not exist "%MSBUILD%" (
    echo error: MSBuild was not found. Install Visual Studio Build Tools.
    exit /b 1
)

echo === Building native miniCOD library ===
call build-discipline-library.cmd
if errorlevel 1 exit /b 1

echo === Building Windows Time input provider ===
call build-time-provider.cmd
if errorlevel 1 exit /b 1

echo === Building Time Card oscillatord service ^(%CONFIG%, x64^) ===
"%MSBUILD%" "%PROJECT%" /t:Rebuild /p:Configuration=%CONFIG% /p:Platform=AnyCPU /p:PlatformTarget=x64 /nologo /v:minimal
if errorlevel 1 exit /b 1

set "OUTPUT=TimeCardOscillatord\bin\%CONFIG%"
if not exist "%OUTPUT%" mkdir "%OUTPUT%"
copy /y "TimeCardDiscipline\THIRD_PARTY_NOTICES.md" "%OUTPUT%\THIRD_PARTY_NOTICES.md" >nul
if errorlevel 1 exit /b 1
copy /y "..\..\Software\oscillatord\third_party\disciplining-minipod\LICENSE" "%OUTPUT%\MINICOD-LICENSE.txt" >nul
if errorlevel 1 exit /b 1
copy /y "TimeCardTimeProvider\bin\Release\TimeCardTimeProvider.dll" "%OUTPUT%\TimeCardTimeProvider.dll" >nul
if errorlevel 1 exit /b 1
if exist "TimeCardTimeProvider\bin\Release\TimeCardTimeProvider.pdb" copy /y "TimeCardTimeProvider\bin\Release\TimeCardTimeProvider.pdb" "%OUTPUT%\TimeCardTimeProvider.pdb" >nul

if not exist "%OUTPUT%\TimeCardOscillatord.exe" (
    echo error: service build did not create %OUTPUT%\TimeCardOscillatord.exe
    exit /b 1
)
if exist "%OUTPUT%\TimeCardDiscipline.dll" (
    echo error: the native miniCOD library was staged as a loose DLL instead of an embedded resource.
    exit /b 1
)

echo.
echo Service: %OUTPUT%\TimeCardOscillatord.exe
endlocal
