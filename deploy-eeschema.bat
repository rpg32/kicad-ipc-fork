@echo off
REM Deploy patched _eeschema.dll to KiCad 10.0 installation
REM IMPORTANT: KiCad 10 uses .dll extension, NOT .kiface
REM
REM Usage: Right-click and "Run as administrator", or run from elevated cmd
REM        eeschema.exe must be closed first.

set SRC=C:\Users\Robert\Programs\kicad-source\build\msvc-win64-release\eeschema\_eeschema.dll
set DST=C:\Program Files\KiCad\10.0\bin\_eeschema.dll

echo Deploying patched eeschema...
echo   From: %SRC%
echo   To:   %DST%
echo.

if not exist "%SRC%" (
    echo ERROR: Build output not found. Run build first.
    pause
    exit /b 1
)

tasklist /FI "IMAGENAME eq eeschema.exe" 2>NUL | find /I "eeschema.exe" >NUL
if %ERRORLEVEL%==0 (
    echo WARNING: eeschema.exe is running. Kill it first.
    pause
    exit /b 1
)

copy /Y "%SRC%" "%DST%"
if %ERRORLEVEL%==0 (
    echo SUCCESS: Deployed _eeschema.dll
) else (
    echo ERROR: Copy failed. Run as administrator?
)
pause
