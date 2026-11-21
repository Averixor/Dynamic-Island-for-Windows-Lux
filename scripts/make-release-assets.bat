@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\.."

REM Must run AFTER a successful app\build.bat so SHA256 matches the smoked exe.
call "%~dp0_version.cmd"
if errorlevel 1 exit /b 1

set EXE=build\Release\Dynamic Island Lux.exe
if not exist "%EXE%" (
  echo Build Release first: app\build.bat
  exit /b 1
)

set OUT=dist\v%VER%
if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%"

copy /Y "%EXE%" "%OUT%\Dynamic Island Lux.exe" >nul
copy /Y LICENSE "%OUT%\LICENSE" >nul
copy /Y NOTICE "%OUT%\NOTICE" >nul
copy /Y INSTALL.txt "%OUT%\INSTALL.txt" >nul
copy /Y RELEASE_NOTES.md "%OUT%\RELEASE_NOTES.md" >nul

REM sha256sum -c compatible: "<lowercase hex>  Dynamic Island Lux.exe"
call "%~dp0_sha256.cmd" "%OUT%\Dynamic Island Lux.exe"
if errorlevel 1 (
  echo FAILED: SHA256 of portable exe
  exit /b 1
)
> "%OUT%\SHA256SUMS.txt" echo %SHA256_HEX%  Dynamic Island Lux.exe
if not exist "%OUT%\SHA256SUMS.txt" (
  echo FAILED: could not write SHA256SUMS.txt
  exit /b 1
)

echo.
echo Assets in %OUT%:
dir /b "%OUT%"
echo.
echo SHA256SUMS.txt:
type "%OUT%\SHA256SUMS.txt"
echo.

echo Verify VERSIONINFO (must be %VER%, not @PROJECT_VERSION@):
REM One line, no bare "|" — cmd treats | as a pipe and can pop "Open with" for leftovers.
powershell -NoProfile -Command "$ver='%VER%'; $v=(Get-Item -LiteralPath '%OUT%\Dynamic Island Lux.exe').VersionInfo; if ($v.FileVersion -like '*@*' -or $v.FileVersion -like '*PROJECT_VERSION*' -or $v.FileVersion -like '*${*') { Write-Error ('Bad FileVersion: ' + $v.FileVersion); exit 1 }; if (-not ($v.FileVersion -like ($ver + '*'))) { Write-Error ('Expected ' + $ver + '*, got ' + $v.FileVersion); exit 1 }; Write-Output ('FileVersion=' + $v.FileVersion); Write-Output ('ProductVersion=' + $v.ProductVersion); Write-Output ('CompanyName=' + $v.CompanyName); Write-Output ('ProductName=' + $v.ProductName); exit 0"
if errorlevel 1 (
  echo VERSIONINFO check FAILED - CMake did not substitute version.rc.in
  exit /b 1
)

echo.
echo Building Setup.exe...
call scripts\make-installer.bat
set "INST_RC=%ERRORLEVEL%"
if not "%INST_RC%"=="0" (
  if defined LUX_REQUIRE_INSTALLER (
    echo FAILED: Setup.exe is required for release ^(installer exit %INST_RC%^)
    exit /b %INST_RC%
  )
  if exist "%OUT%\DynamicIslandLux-Setup-%VER%.exe" (
    echo NOTE: Setup.exe exists but post-steps failed ^(exit %INST_RC%^) - check SHA256SUMS
    exit /b %INST_RC%
  )
  echo NOTE: installer skipped ^(ISCC missing or compile failed^) - portable assets still OK
)
if defined LUX_REQUIRE_INSTALLER (
  if not exist "%OUT%\DynamicIslandLux-Setup-%VER%.exe" (
    echo FAILED: Setup.exe missing from %OUT%
    exit /b 1
  )
)

echo.
echo OK - attach %OUT%\* to the GitHub Release for tag v%VER%
echo Next: git tag -a v%VER% -m "Dynamic Island Lux v%VER%"
exit /b 0
