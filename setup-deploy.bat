@echo off
icacls "C:\Program Files\KiCad\10.0\bin" /grant "%USERNAME%:(OI)(CI)M" /T >nul 2>&1
taskkill /F /IM pcbnew.exe >nul 2>&1
timeout /t 1 /nobreak >nul
copy /Y C:\Users\Robert\Programs\kicad-source\build\msvc-win64-release\pcbnew\_pcbnew.dll "C:\Program Files\KiCad\10.0\bin\_pcbnew.dll"
echo SETUP+DEPLOY DONE
