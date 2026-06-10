@echo off
set BUILD=C:\Users\Robert\Programs\kicad-source\build\msvc-win64-release
set DEST=C:\Program Files\KiCad\10.0\bin

echo Copying KiCad core binaries...
copy /Y "%BUILD%\eeschema\_eeschema.dll" "%DEST%\_eeschema.dll"
copy /Y "%BUILD%\eeschema\eeschema.exe" "%DEST%\eeschema.exe"
copy /Y "%BUILD%\pcbnew\_pcbnew.dll" "%DEST%\_pcbnew.dll"
copy /Y "%BUILD%\pcbnew\pcbnew.exe" "%DEST%\pcbnew.exe"
copy /Y "%BUILD%\common\kicommon.dll" "%DEST%\kicommon.dll"
copy /Y "%BUILD%\api\kiapi.dll" "%DEST%\kiapi.dll"
copy /Y "%BUILD%\common\gal\kigal.dll" "%DEST%\kigal.dll"
copy /Y "%BUILD%\kicad\kicad-cli.exe" "%DEST%\kicad-cli.exe"

echo Patched!
pause
