@echo off
setlocal
cd /d "%~dp0\.."
set EXE=build\Release\DynamicIslandLux.exe
if not exist "%EXE%" (
  echo Build Release first: app\build.bat
  exit /b 1
)
set OUT=dist\v1.0.0
mkdir "%OUT%" 2>nul
copy /Y "%EXE%" "%OUT%\DynamicIslandLux.exe" >nul
copy /Y LICENSE "%OUT%\LICENSE" >nul
copy /Y INSTALL.txt "%OUT%\INSTALL.txt" >nul
copy /Y RELEASE_NOTES.md "%OUT%\RELEASE_NOTES.md" >nul
certutil -hashfile "%OUT%\DynamicIslandLux.exe" SHA256 > "%OUT%\SHA256SUMS.txt"
echo.
echo Assets in %OUT%:
dir /b "%OUT%"
echo.
echo Verify VERSIONINFO:
powershell -NoProfile -Command "(Get-Item '%OUT%\DynamicIslandLux.exe').VersionInfo | Format-List FileVersion,ProductVersion,CompanyName,ProductName"
endlocal
