@echo off
setlocal

rem Keep in sync with build.bat's Qt path
set QT_DIR=D:\QT\6.11.2\msvc2022_64
set ROOT=%~dp0..
set EXE=%ROOT%\build\src\app\Pic2Pet.exe

if not exist "%EXE%" (
    echo [ERROR] not found: %EXE%  -- run scripts\build.bat first
    exit /b 1
)

rem Add Qt DLLs to PATH during development (avoids running windeployqt every time)
set PATH=%QT_DIR%\bin;%PATH%

echo [run] %EXE% %*
rem Enable console logging: on Windows Qt sends qInfo/qDebug to OutputDebugString by
rem default, so without this you get the window but no logs at all.
set QT_FORCE_STDERR_LOGGING=1
"%EXE%" %*
