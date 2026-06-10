@echo off
set BUILD=C:\Users\Robert\Programs\kicad-source\build\msvc-win64-release
set DEST=C:\Program Files\KiCad\10.0\bin

copy /Y "%BUILD%\common\kicommon.dll" "%DEST%\kicommon.dll"
if %ERRORLEVEL%==0 (echo DEPLOYED: kicommon.dll) else (echo FAILED: kicommon.dll)

copy /Y "%BUILD%\eeschema\_eeschema.dll" "%DEST%\_eeschema.dll"
if %ERRORLEVEL%==0 (echo DEPLOYED: _eeschema.dll) else (echo FAILED: _eeschema.dll)

copy /Y "%BUILD%\pcbnew\_pcbnew.dll" "%DEST%\_pcbnew.dll"
if %ERRORLEVEL%==0 (echo DEPLOYED: _pcbnew.dll) else (echo FAILED: _pcbnew.dll)

echo.
echo Done. Restart KiCad apps.
