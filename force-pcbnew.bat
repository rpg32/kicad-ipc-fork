@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=%PATH%;C:\Users\Robert\Programs\kicad-win-builder\.support\swigwin-4.3.1
set VCPKG_ROOT=C:\Users\Robert\Programs\kicad-win-builder\vcpkg
cd /d C:\Users\Robert\Programs\kicad-source\build\msvc-win64-release
echo BUILD START %TIME% > C:\Users\Robert\Programs\kicad-source\force.log
ninja _pcbnew.dll >> C:\Users\Robert\Programs\kicad-source\force.log 2>&1
echo NINJA_DONE rc=%ERRORLEVEL% %TIME% >> C:\Users\Robert\Programs\kicad-source\force.log
taskkill /F /IM pcbnew.exe >nul 2>&1
timeout /t 2 /nobreak >nul
copy /Y pcbnew\_pcbnew.dll "C:\Program Files\KiCad\10.0\bin\_pcbnew.dll" >> C:\Users\Robert\Programs\kicad-source\force.log 2>&1
echo DEPLOY rc=%ERRORLEVEL% %TIME% >> C:\Users\Robert\Programs\kicad-source\force.log
