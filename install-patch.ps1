Stop-Process -Name eeschema -Force -ErrorAction SilentlyContinue
Start-Sleep 1
Copy-Item 'C:\Program Files\KiCad\10.0\bin\_eeschema.dll' 'C:\Program Files\KiCad\10.0\bin\_eeschema.dll.original' -Force
Copy-Item 'C:\Users\Robert\Programs\kicad-source\build\msvc-win64-release\eeschema\_eeschema.dll' 'C:\Program Files\KiCad\10.0\bin\_eeschema.dll' -Force
'PATCHED' | Out-File 'C:\Users\Robert\Programs\kicad-source\patch-result.txt'
