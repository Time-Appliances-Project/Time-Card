@echo off
rem Build Orolia's miniCOD algorithm as an x64 Windows DLL.
setlocal
cd /d "%~dp0"

set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath`) do set "VSROOT=%%i"
)
set "LLVM=%VSROOT%\VC\Tools\Llvm\x64\bin\clang-cl.exe"
if not exist "%LLVM%" (
    echo error: clang-cl was not found. Install the Visual Studio C++ Clang tools.
    exit /b 1
)
set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo error: the Visual Studio x64 build environment was not found.
    exit /b 1
)

set "MINIPOD=..\..\Software\oscillatord\third_party\disciplining-minipod"
set "OUT=TimeCardDiscipline\bin\Release"
if not exist "%OUT%" mkdir "%OUT%"

call "%VCVARS%" >nul
"%LLVM%" /nologo /LD /O2 /MD /W4 /D_CRT_SECURE_NO_WARNINGS /DPACKAGE_VERSION=\"3.6.0\" ^
  /I"%MINIPOD%\include" /I"%MINIPOD%\src" ^
  TimeCardDiscipline\timecard_discipline.c ^
  "%MINIPOD%\src\checks.c" "%MINIPOD%\src\fine_circular_buffer.c" ^
  "%MINIPOD%\src\log.c" "%MINIPOD%\src\oscillator-disciplining.c" ^
  "%MINIPOD%\src\phase.c" "%MINIPOD%\src\utils.c" ^
  /link /OUT:"%OUT%\TimeCardDiscipline.dll" /IMPLIB:"%OUT%\TimeCardDiscipline.lib"
if errorlevel 1 exit /b 1
echo Library: %OUT%\TimeCardDiscipline.dll
endlocal
