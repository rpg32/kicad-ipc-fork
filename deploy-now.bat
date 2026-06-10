@echo off
set BUILD=C:\Users\Robert\Programs\kicad-source\build\msvc-win64-release
set DEST=C:\Program Files\KiCad\10.0\bin
copy /Y "%BUILD%\common\kicommon.dll" "%DEST%\kicommon.dll"
copy /Y "%BUILD%\eeschema\_eeschema.dll" "%DEST%\_eeschema.dll"
copy /Y "%BUILD%\pcbnew\_pcbnew.dll" "%DEST%\_pcbnew.dll"
