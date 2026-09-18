@echo off
setlocal

rem 与 build.bat 保持一致的 Qt 路径
set QT_DIR=D:\QT\6.11.2\msvc2022_64
set ROOT=%~dp0..
set EXE=%ROOT%\build\src\app\Pic2Pet.exe

if not exist "%EXE%" (
    echo [ERROR] 找不到 %EXE%，请先运行 scripts\build.bat
    exit /b 1
)

rem 开发期把 Qt 的 DLL 放进 PATH，省去每次 windeployqt
set PATH=%QT_DIR%\bin;%PATH%

echo [run] %EXE% %*
rem 把控制台日志打开：Qt 在 Windows 上默认把 qInfo/qDebug 送到 OutputDebugString，
rem 不设这个变量的话窗口起来了但什么日志都看不到。
set QT_FORCE_STDERR_LOGGING=1
"%EXE%" %*
