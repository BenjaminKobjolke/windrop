@echo off
REM Build build\Release\windrop.exe with the CMake that ships with Visual Studio,
REM so no separate CMake install is needed. CMake picks the newest Visual Studio
REM generator by itself.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found - install Visual Studio with the C++ Desktop workload.
    exit /b 1
)
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo No Visual Studio with the C++ x64 tools found.
    exit /b 1
)
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
    echo CMake not found in %VSDIR% - add the "C++ CMake tools for Windows" component.
    exit /b 1
)
pushd "%~dp0.."
"%CMAKE%" -B build -S . -A x64
if errorlevel 1 goto done
"%CMAKE%" --build build --config Release
:done
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
