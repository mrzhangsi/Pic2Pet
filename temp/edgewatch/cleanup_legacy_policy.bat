@echo off
REM Clean up leftover Edge policy keys written by the old (now-removed) web filter feature.
REM Requires Administrator. Run in an elevated cmd.
REM
REM Background: Pic2Pet's first white-list implementation wrote real Edge policies to
REM   HKLM\SOFTWARE\Policies\Microsoft\Edge\URLBlocklist / URLAllowlist
REM and kept a backup under HKLM\SOFTWARE\Pic2Pet\WebFilterBackup.
REM That whole approach was replaced by read-only detection (src/monitor), so the new
REM version never writes any registry value. These leftovers are from the old tests.
REM
REM The key is removed only if it still contains exactly the values we wrote
REM (preset.example.com). Anything else is left untouched.

setlocal

set BLOCK=HKLM\SOFTWARE\Policies\Microsoft\Edge\URLBlocklist
set ALLOW=HKLM\SOFTWARE\Policies\Microsoft\Edge\URLAllowlist
set BACKUP=HKLM\SOFTWARE\Pic2Pet

net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Please run this in an Administrator command prompt.
    pause
    exit /b 1
)

REM --- 1. remove only our leftover test value, keep anything the user configured ---
for %%K in ("%BLOCK%" "%ALLOW%") do (
    if exist %%K (
        echo [check] %%K
        for /f "tokens=2,*" %%A in ('reg query %%K 2^>nul ^| findstr /i "preset.example.com"') do (
            echo [remove] %%A
            reg delete "%%K\1" /f >nul 2>&1
        )
        rem if the key is now empty, drop it
        reg query %%K >nul 2>&1
        if errorlevel 1 (
            echo [remove] empty key %%K
            reg delete %%K /f >nul 2>&1
        ) else (
            echo [keep]   %%K still has values, left untouched
        )
    ) else (
        echo [skip]   %%K not present
    )
)

REM --- 2. our backup key: only remove if every value is one of ours ---
if exist "%BACKUP%" (
    echo [check] %BACKUP%
    set /p found=<nul
    for /f "tokens=2,*" %%A in ('reg query "%BACKUP%" /s 2^>nul ^| findstr /i "preset.example.com"') do set found=1
    if defined found (
        echo [remove] %BACKUP% (only contains preset.example.com)
        reg delete "%BACKUP%" /f >nul 2>&1
    ) else (
        echo [keep]   %BACKUP% contains values that are not ours, left untouched
    )
    rem drop HKLM\SOFTWARE\Pic2Pet too if it became empty
    reg query "%BACKUP%" >nul 2>&1
    if errorlevel 1 (
        reg delete "%BACKUP%" /f >nul 2>&1
        echo [remove] %BACKUP%
    )
) else (
    echo [skip]   %BACKUP% not present
)

echo.
echo Done. Verify:
reg query "HKLM\SOFTWARE\Policies\Microsoft\Edge" 2>nul || echo   (Edge policy key is gone)
reg query "HKLM\SOFTWARE\Pic2Pet" 2>nul || echo   (Pic2Pet backup key is gone)
pause
