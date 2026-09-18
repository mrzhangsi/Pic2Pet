@echo off
setlocal

rem ============================================================
rem  Pic2Pet packaging script: release build (no console) + windeployqt
rem  Usage:  scripts\deploy.bat
rem  Output: dist\Pic2Pet.exe (copy the whole folder, or zip it)
rem ============================================================

set QT_DIR=D:\QT\6.11.2\msvc2022_64
set VS_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
set CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe
set NINJA_EXE=C:\Users\ablkuv\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe

set ROOT=%~dp0..
set BUILD=%ROOT%\build-release
set DIST=%ROOT%\dist

echo [1/4] init MSVC environment
call "%VS_VCVARS%"
if errorlevel 1 exit /b 1

echo [2/4] configure + build (Release, no console subsystem)
if not exist "%BUILD%" mkdir "%BUILD%"
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD%" -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
      -DPIC2PET_CONSOLE=OFF
if errorlevel 1 exit /b 1

"%CMAKE_EXE%" --build "%BUILD%"
if errorlevel 1 exit /b 1

echo [3/4] deploy Qt runtime to %DIST%
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%"
copy /y "%BUILD%\src\app\Pic2Pet.exe" "%DIST%\" >nul

"%QT_DIR%\bin\windeployqt.exe" "%DIST%\Pic2Pet.exe" ^
      --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
if errorlevel 1 exit /b 1

rem ---- prune: 46.8MB -> 29.1MB ----
rem Only desktop OpenGL is used. The files below are pulled in by plugin dependency
rem chains and verified safe to delete:
rem   dxcompiler/dxil        shader compiler for Qt's D3D11 RHI (~15MB, the biggest chunk)
rem   Qt6Network/Svg         pulled in by qtuiotouchplugin / qsvgicon
rem   networkinformation/tls same chain
del /q "%DIST%\dxcompiler.dll" 2>nul
del /q "%DIST%\dxil.dll" 2>nul
del /q "%DIST%\Qt6Network.dll" 2>nul
del /q "%DIST%\Qt6Svg.dll" 2>nul
del /q "%DIST%\generic\qtuiotouchplugin.dll" 2>nul
del /q "%DIST%\iconengines\qsvgicon.dll" 2>nul
del /q "%DIST%\imageformats\qsvg.dll" 2>nul
del /q "%DIST%\networkinformation\qnetworklistmanager.dll" 2>nul
del /q "%DIST%\tls\qcertonlybackend.dll" 2>nul
del /q "%DIST%\tls\qschannelbackend.dll" 2>nul
if exist "%DIST%\generic" rmdir /s /q "%DIST%\generic"
if exist "%DIST%\iconengines" rmdir /s /q "%DIST%\iconengines"
if exist "%DIST%\networkinformation" rmdir /s /q "%DIST%\networkinformation"
if exist "%DIST%\tls" rmdir /s /q "%DIST%\tls"

echo [4/4] done
echo       output: %DIST%\Pic2Pet.exe (copy the whole folder, ~29MB)
echo       note: --no-compiler-runtime means the VC++ runtime is NOT bundled;
echo             the target machine needs VS 2015-2022 Redistributable.
echo             Drop that flag to bundle it.
