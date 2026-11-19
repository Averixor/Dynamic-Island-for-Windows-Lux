@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

REM Must run AFTER a successful app\build.bat so SHA256 matches the smoked exe.
set VER=1.0.3
set EXE=build\Release\DynamicIslandLux.exe
if not exist "%EXE%" (
  echo Build Release first: app\build.bat
  exit /b 1
)

set OUT=dist\v%VER%
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

echo Verify VERSIONINFO (must be %VER%, not @PROJECT_VERSION@):
REM One line, no bare "|" — cmd treats | as a pipe and can pop "Open with" for leftovers.
powershell -NoProfile -Command "$ver='%VER%'; $v=(Get-Item -LiteralPath '%OUT%\DynamicIslandLux.exe').VersionInfo; if ($v.FileVersion -like '*@*' -or $v.FileVersion -like '*PROJECT_VERSION*' -or $v.FileVersion -like '*${*') { Write-Error ('Bad FileVersion: ' + $v.FileVersion); exit 1 }; if (-not ($v.FileVersion -like ($ver + '*'))) { Write-Error ('Expected ' + $ver + '*, got ' + $v.FileVersion); exit 1 }; Write-Output ('FileVersion=' + $v.FileVersion); Write-Output ('ProductVersion=' + $v.ProductVersion); Write-Output ('CompanyName=' + $v.CompanyName); Write-Output ('ProductName=' + $v.ProductName)"
if errorlevel 1 (
  echo VERSIONINFO check FAILED - CMake did not substitute version.rc.in
  exit /b 1
)

echo.
echo Optional: build Setup.exe if Inno Setup is installed...
call scripts\make-installer.bat
if errorlevel 1 (
  echo NOTE: installer skipped ^(ISCC missing or compile failed^) - portable assets still OK
)

echo.
echo OK - attach %OUT%\* to the GitHub Release for tag v%VER%
echo Next: git tag -a v%VER% -m "Dynamic Island Lux standalone v%VER%"
exit /b 0
