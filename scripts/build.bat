@echo off
setlocal

rem ============================================================
rem  Pic2Pet 构建脚本（Windows / MSVC 2022 / Qt 6 / Ninja）
rem  用法：
rem     scripts\build.bat          配置 + 编译
rem     scripts\build.bat clean    清空 build 目录
rem     scripts\build.bat deploy   编译后顺便跑 windeployqt
rem ============================================================

set QT_DIR=D:\QT\6.11.2\msvc2022_64
set VS_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat

rem 独立安装的 CMake / Ninja（winget）
set CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe
set NINJA_EXE=C:\Users\ablkuv\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe

rem 上面两个找不到时，回退到 Qt 安装器自带的
if not exist "%CMAKE_EXE%" if exist "D:\QT\Tools\CMake_64\bin\cmake.exe" set CMAKE_EXE=D:\QT\Tools\CMake_64\bin\cmake.exe
if not exist "%NINJA_EXE%" if exist "D:\QT\Tools\Ninja\ninja.exe"        set NINJA_EXE=D:\QT\Tools\Ninja\ninja.exe

set ROOT=%~dp0..
set BUILD=%ROOT%\build

if /i "%1"=="clean" (
    echo [clean] 删除 %BUILD%
    if exist "%BUILD%" rmdir /s /q "%BUILD%"
    echo [clean] 完成
    exit /b 0
)

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [ERROR] 找不到 Qt：%QT_DIR%
    exit /b 1
)
if not exist "%VS_VCVARS%" (
    echo [ERROR] 找不到 %VS_VCVARS%
    exit /b 1
)
if not exist "%CMAKE_EXE%" (
    echo [ERROR] 找不到 cmake.exe，请修改脚本里的 CMAKE_EXE
    exit /b 1
)
if not exist "%NINJA_EXE%" (
    echo [ERROR] 找不到 ninja.exe，请修改脚本里的 NINJA_EXE
    exit /b 1
)

echo [0/3] 初始化 MSVC 环境
call "%VS_VCVARS%"
if errorlevel 1 exit /b 1

rem 显式钉死编译器，防止 PATH 里的 MSYS2/MinGW g++ 被 CMake 误选
if defined VCToolsInstallDir (
    set CXX_COMPILER=%VCToolsInstallDir%bin\Hostx64\x64\cl.exe
    set C_COMPILER=%VCToolsInstallDir%bin\Hostx64\x64\cl.exe
) else (
    set CXX_COMPILER=cl.exe
    set C_COMPILER=cl.exe
)
echo       CXX = %CXX_COMPILER%

rem 开发模式：保留控制台子系统，方便看 [perf] 日志
set CONSOLE_FLAG=-DPIC2PET_CONSOLE=ON
if /i "%1"=="release" set CONSOLE_FLAG=-DPIC2PET_CONSOLE=OFF
if /i "%1"=="deploy"  set CONSOLE_FLAG=-DPIC2PET_CONSOLE=OFF

echo [1/3] 配置（Ninja / Release）
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD%" -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
      -DCMAKE_C_COMPILER="%C_COMPILER%" ^
      -DCMAKE_CXX_COMPILER="%CXX_COMPILER%" ^
      %CONSOLE_FLAG%
if errorlevel 1 (
    echo [ERROR] CMake 配置失败
    exit /b 1
)

echo [2/3] 编译
"%CMAKE_EXE%" --build "%BUILD%"
if errorlevel 1 (
    echo [ERROR] 编译失败
    exit /b 1
)

echo [3/3] 完成
echo      输出：%BUILD%\src\app\Pic2Pet.exe

if /i "%1"=="deploy" (
    echo [deploy] 拷贝 Qt 运行时
    "%QT_DIR%\bin\windeployqt.exe" "%BUILD%\src\app\Pic2Pet.exe"
)

echo.
echo [OK] 运行：scripts\run.bat
