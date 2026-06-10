@echo off
set B=C:\Users\Robert\Programs\kicad-source\build\msvc-win64-release
set D=C:\Program Files\KiCad\10.0\bin
taskkill /F /IM pcbnew.exe >nul 2>&1
taskkill /F /IM eeschema.exe >nul 2>&1
taskkill /F /IM kicad.exe >nul 2>&1
timeout /t 2 /nobreak >nul
if not exist "%D%\kiapi.dll.stock" copy /Y "%D%\kiapi.dll" "%D%\kiapi.dll.stock"
if not exist "%D%\kicommon.dll.stock" copy /Y "%D%\kicommon.dll" "%D%\kicommon.dll.stock"
if not exist "%D%\_pcbnew.dll.stock" copy /Y "%D%\_pcbnew.dll" "%D%\_pcbnew.dll.stock"
copy /Y "%B%\api\kiapi.dll" "%D%\kiapi.dll"
copy /Y "%B%\common\kicommon.dll" "%D%\kicommon.dll"
copy /Y "%B%\pcbnew\_pcbnew.dll" "%D%\_pcbnew.dll"
echo === PNS-patched DLLs deployed (kiapi + kicommon + _pcbnew). Stock saved as *.stock ===
