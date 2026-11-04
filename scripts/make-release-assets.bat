@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

REM Must run AFTER a successful app\build.bat so SHA256 matches the smoked exe.
set EXE=build\Release\DynamicIslandLux.exe
if not exist "%EXE%" (
  echo Build Release first: app\build.bat
  exit /b 1
)

set OUT=dist\v1.0.0
if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%"

copy /Y "%EXE%" "%OUT%\DynamicIslandLux.exe" >nul
copy /Y LICENSE "%OUT%\LICENSE" >nul
copy /Y INSTALL.txt "%OUT%\INSTALL.txt" >nul
copy /Y RELEASE_NOTES.md "%OUT%\RELEASE_NOTES.md" >nul

REM sha256sum -c compatible: "<lowercase hex>  DynamicIslandLux.exe"
powershell -NoProfile -Command ^
  "$h=(Get-FileHash -Algorithm SHA256 '%OUT%\DynamicIslandLux.exe').Hash.ToLowerInvariant();" ^
  "Set-Content -Encoding ascii '%OUT%\SHA256SUMS.txt' -Value ($h + '  DynamicIslandLux.exe')"
if errorlevel 1 exit /b 1

echo.
echo Assets in %OUT%:
dir /b "%OUT%"
echo.
echo SHA256SUMS.txt:
type "%OUT%\SHA256SUMS.txt"
echo.

echo Verify VERSIONINFO (must be 1.0.0, not @PROJECT_VERSION@):
powershell -NoProfile -Command ^
  "$v=(Get-Item '%OUT%\DynamicIslandLux.exe').VersionInfo;" ^
  "if ($v.FileVersion -match '@|PROJECT_VERSION|\$\{') { Write-Error ('Bad FileVersion: ' + $v.FileVersion); exit 1 };" ^
  "if (-not ($v.FileVersion -like '1.0.0*')) { Write-Error ('Expected 1.0.0*, got ' + $v.FileVersion); exit 1 };" ^
  "$v | Format-List FileVersion,ProductVersion,CompanyName,ProductName"
if errorlevel 1 (
  echo VERSIONINFO check FAILED - CMake did not substitute version.rc.in
  exit /b 1
)

echo.
echo OK - attach %OUT%\* to the GitHub Release for tag v1.0.0
echo Next: git tag -a v1.0.0 -m "Dynamic Island Lux standalone v1.0.0"
exit /b 0
