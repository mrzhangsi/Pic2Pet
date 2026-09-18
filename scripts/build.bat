@echo off
setlocal

rem ============================================================
rem  Pic2Pet build script (Windows / MSVC 2022 / Qt 6 / Ninja)
rem  Usage:
rem     scripts\build.bat          configure + build
rem     scripts\build.bat clean    wipe the build dir
rem     scripts\build.bat deploy   build, then run windeployqt
rem ============================================================

rem Local default: Qt 6.11.2 (installed here; everything >= 6.10 is free of the black-bg bug).
rem NOTE: CI/Release uses 6.10.3 -- Qt changed the repo layout in 6.11, so aqtinstall cannot
rem       install 6.11+ on CI.
rem (6.9.x has the QTBUG-136098 transparent-black regression, fixed in 6.10.0, so 6.9 is unusable.)
rem Override with another version: set PIC2PET_QT=D:\QT\6.x.y\msvc2022_64
if defined PIC2PET_QT (set QT_DIR=%PIC2PET_QT%) else (set QT_DIR=D:\QT\6.11.2\msvc2022_64)
set VS_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat

rem Standalone CMake / Ninja (winget)
set CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe
set NINJA_EXE=C:\Users\ablkuv\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe

rem Fall back to the ones shipped with Qt if the above are missing
if not exist "%CMAKE_EXE%" if exist "D:\QT\Tools\CMake_64\bin\cmake.exe" set CMAKE_EXE=D:\QT\Tools\CMake_64\bin\cmake.exe
if not exist "%NINJA_EXE%" if exist "D:\QT\Tools\Ninja\ninja.exe"        set NINJA_EXE=D:\QT\Tools\Ninja\ninja.exe

set ROOT=%~dp0..
set BUILD=%ROOT%\build

if /i "%1"=="clean" (
    echo [clean] removing %BUILD%
    if exist "%BUILD%" rmdir /s /q "%BUILD%"
    echo [clean] done
    exit /b 0
)

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [ERROR] Qt not found: %QT_DIR%
    exit /b 1
)
if not exist "%VS_VCVARS%" (
    echo [ERROR] not found: %VS_VCVARS%
    exit /b 1
)
if not exist "%CMAKE_EXE%" (
    echo [ERROR] cmake.exe not found -- edit CMAKE_EXE in this script
    exit /b 1
)
if not exist "%NINJA_EXE%" (
    echo [ERROR] ninja.exe not found -- edit NINJA_EXE in this script
    exit /b 1
)

echo [0/3] init MSVC environment
call "%VS_VCVARS%"
if errorlevel 1 exit /b 1

rem Pin the compiler explicitly so a MSYS2/MinGW g++ in PATH is not picked up by CMake
if defined VCToolsInstallDir (
    set CXX_COMPILER=%VCToolsInstallDir%bin\Hostx64\x64\cl.exe
    set C_COMPILER=%VCToolsInstallDir%bin\Hostx64\x64\cl.exe
) else (
    set CXX_COMPILER=cl.exe
    set C_COMPILER=cl.exe
)
echo       CXX = %CXX_COMPILER%

rem Dev mode: keep the console subsystem so [perf] logs are visible
set CONSOLE_FLAG=-DPIC2PET_CONSOLE=ON
if /i "%1"=="release" set CONSOLE_FLAG=-DPIC2PET_CONSOLE=OFF
if /i "%1"=="deploy"  set CONSOLE_FLAG=-DPIC2PET_CONSOLE=OFF

echo [1/3] configure (Ninja / Release)
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD%" -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
      -DCMAKE_C_COMPILER="%C_COMPILER%" ^
      -DCMAKE_CXX_COMPILER="%CXX_COMPILER%" ^
      %CONSOLE_FLAG%
if errorlevel 1 (
    echo [ERROR] cmake configure failed
    exit /b 1
)

echo [2/3] build
"%CMAKE_EXE%" --build "%BUILD%"
if errorlevel 1 (
    echo [ERROR] build failed
    exit /b 1
)

echo [3/3] done
echo      output: %BUILD%\src\app\Pic2Pet.exe

if /i "%1"=="deploy" (
    echo [deploy] copying Qt runtime
    "%QT_DIR%\bin\windeployqt.exe" "%BUILD%\src\app\Pic2Pet.exe"
)

echo.
echo [OK] run: scripts\run.bat
