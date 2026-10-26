@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM  One-step build for Taal Vidya (JUCE, VST3 + Standalone, MSVC)
REM
REM    build.bat            Release build
REM    build.bat debug      Debug build
REM    build.bat clean      wipe build dir first, then Release build
REM    build.bat install    also copy the VST3 to C:\Program Files\Common Files\VST3
REM
REM  Output ends up in .\dist
REM ---------------------------------------------------------------------------
cd /d "%~dp0"

set CONFIG=Release
set CLEAN=0
set INSTALL=0
for %%A in (%*) do (
    if /I "%%A"=="debug"   set CONFIG=Debug
    if /I "%%A"=="clean"   set CLEAN=1
    if /I "%%A"=="install" set INSTALL=1
)

REM --- JUCE ------------------------------------------------------------------
if not exist "libs\JUCE\CMakeLists.txt" (
    echo [build] Fetching JUCE 8.0.8...
    git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git libs\JUCE || goto :fail
)

REM --- MSVC environment ------------------------------------------------------
where cl >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo [build] Visual Studio Build Tools not found. Install them with:
        echo     winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
        goto :fail
    )
    for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
    if not defined VSDIR (
        echo [build] MSVC C++ tools not installed in Visual Studio.
        goto :fail
    )
    call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul || goto :fail
)

REM Prefer the CMake/Ninja bundled with VS over any MSYS/MinGW ones on PATH
if defined VSDIR set "PATH=!VSDIR!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;!VSDIR!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;!PATH!"

REM --- configure + build -----------------------------------------------------
set BUILD_DIR=build-msvc
if "%CLEAN%"=="1" if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

echo [build] Configuring (%CONFIG%)...
cmake -S . -B %BUILD_DIR% -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl || goto :fail

echo [build] Building...
cmake --build %BUILD_DIR% --config %CONFIG% || goto :fail

REM --- collect output --------------------------------------------------------
if exist dist rmdir /s /q dist
mkdir dist
set ART=%BUILD_DIR%\TablaVST_artefacts\%CONFIG%
if exist "%ART%\Standalone\Taal Vidya.exe" copy /y "%ART%\Standalone\Taal Vidya.exe" dist\ >nul
if exist "%ART%\VST3\Taal Vidya.vst3" xcopy /e /i /y "%ART%\VST3\Taal Vidya.vst3" "dist\VST3\Taal Vidya.vst3\" >nul

REM Optionally copy the plugin to a VST folder: set TABLA_VST_DIR, or put it in build.local.bat (git-ignored)
if not defined TABLA_VST_DIR if exist "%~dp0build.local.bat" call "%~dp0build.local.bat"
if exist "%TABLA_VST_DIR%\" (
    set "DEST=%TABLA_VST_DIR%\TablaVST"
    if not exist "!DEST!" mkdir "!DEST!"
    if exist "!DEST!\Taal Vidya.vst3" rmdir /s /q "!DEST!\Taal Vidya.vst3"
    xcopy /e /i /y "dist\VST3\Taal Vidya.vst3" "!DEST!\Taal Vidya.vst3\" >nul && echo [build] VST3 copied to !DEST! || echo [build] Could not copy VST3 ^(is the DAW running and locking it?^)
)

if "%INSTALL%"=="1" (
    echo [build] Installing VST3 to C:\Program Files\Common Files\VST3 ^(needs admin^)...
    xcopy /e /i /y "dist\VST3\*" "C:\Program Files\Common Files\VST3\" || echo [build] Install failed - run from an elevated prompt.
)

echo.
echo [build] Done. Output in %CD%\dist
dir /b dist
exit /b 0

:fail
echo.
echo [build] FAILED
exit /b 1
