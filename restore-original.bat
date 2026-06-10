@echo off
taskkill /IM eeschema.exe /F 2>nul
timeout /t 2 >nul
copy /Y "C:\Program Files\KiCad\10.0\bin\_eeschema.dll.original" "C:\Program Files\KiCad\10.0\bin\_eeschema.dll"
echo Restored original DLL
echo RESTORED > "C:\Users\Robert\Programs\kicad-source\patch-result.txt"
pause
