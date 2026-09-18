@echo off
setlocal

rem ============================================================
rem  Pic2Pet 打包脚本：编译 Release（无控制台）+ windeployqt 拷贝 Qt 运行时
rem  用法：scripts\deploy.bat
rem  产物：dist\Pic2Pet.exe（可整个目录拷走或压缩分发）
rem ============================================================

set QT_DIR=D:\QT\6.11.2\msvc2022_64
set VS_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
set CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe
set NINJA_EXE=C:\Users\ablkuv\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe

set ROOT=%~dp0..
set BUILD=%ROOT%\build-release
set DIST=%ROOT%\dist

echo [1/4] 初始化 MSVC 环境
call "%VS_VCVARS%"
if errorlevel 1 exit /b 1

echo [2/4] 配置 + 编译（Release，无控制台子系统）
if not exist "%BUILD%" mkdir "%BUILD%"
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD%" -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
      -DPIC2PET_CONSOLE=OFF
if errorlevel 1 exit /b 1

"%CMAKE_EXE%" --build "%BUILD%"
if errorlevel 1 exit /b 1

echo [3/4] 部署 Qt 运行时到 %DIST%
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%"
copy /y "%BUILD%\src\app\Pic2Pet.exe" "%DIST%\" >nul

"%QT_DIR%\bin\windeployqt.exe" "%DIST%\Pic2Pet.exe" ^
      --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
if errorlevel 1 exit /b 1

rem ---- 精简：46.8MB -> 29.1MB ----
rem 只用 desktop OpenGL，以下都是 windeployqt 因插件依赖链带进来的，实测删掉不影响运行：
rem   dxcompiler/dxil   Qt 的 D3D11 RHI 用的着色器编译器（约 15MB，最大头）
rem   Qt6Network/Svg    由 qtuiotouchplugin / qsvgicon 拉进来
rem   网络信息 / TLS     同上
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

echo [4/4] 完成
echo       产物：%DIST%\Pic2Pet.exe（整个目录拷走即可，约 29MB）
echo       注意：--no-compiler-runtime 表示不打包 VC++ 运行库，
echo             目标机器需已安装 VS 2015-2022 Redistributable。
echo             若要自带运行库，去掉该参数即可。
