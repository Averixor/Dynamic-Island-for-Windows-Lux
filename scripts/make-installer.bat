@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\.."

set VER=1.0.2
set EXE=build\Release\DynamicIslandLux.exe
set ISS=installer\DynamicIslandLux.iss
set OUT=dist\v%VER%\DynamicIslandLux-Setup-%VER%.exe

if not exist "%EXE%" (
  echo Release exe missing - building first...
  call app\build.bat
  if errorlevel 1 exit /b %ERRORLEVEL%
)

if not exist "%ISS%" (
  echo Missing %ISS%
  exit /b 1
)

set ISCC=
if exist "%LocalAppData%\Programs\Inno Setup 7\ISCC.exe" set "ISCC=%LocalAppData%\Programs\Inno Setup 7\ISCC.exe"
if exist "%ProgramFiles%\Inno Setup 7\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
if exist "%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe"
if exist "%LocalAppData%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
where iscc >nul 2>&1 && for /f "delims=" %%I in ('where iscc') do if not defined ISCC set "ISCC=%%I"

if not defined ISCC (
  echo Inno Setup compiler ^(ISCC.exe^) not found.
  echo Install with:
  echo   winget install --id JRSoftware.InnoSetup.7 -e --accept-package-agreements --accept-source-agreements
  echo Or: winget install --id JRSoftware.InnoSetup -e --accept-package-agreements --accept-source-agreements
  exit /b 2
)

if not exist "dist\v%VER%" mkdir "dist\v%VER%"

echo Using ISCC: %ISCC%
"%ISCC%" "/DMyAppVersion=%VER%" "%ISS%"
if errorlevel 1 (
  echo FAILED: Inno Setup compile
  exit /b %ERRORLEVEL%
)

if not exist "%OUT%" (
  echo FAILED: expected output missing: %OUT%
  exit /b 1
)

powershell -NoProfile -Command ^
  "$setup='%OUT%';" ^
  "$sums='dist\v%VER%\SHA256SUMS.txt';" ^
  "$h=(Get-FileHash -Algorithm SHA256 $setup).Hash.ToLowerInvariant();" ^
  "$line=$h + '  DynamicIslandLux-Setup-%VER%.exe';" ^
  "if (Test-Path $sums) {" ^
  "  $c=Get-Content $sums | Where-Object { $_ -notmatch 'DynamicIslandLux-Setup-' };" ^
  "  Set-Content -Encoding ascii $sums -Value ($c + $line)" ^
  "} else { Set-Content -Encoding ascii $sums -Value $line };" ^
  "Write-Host $line"

echo.
echo OK - installer: %OUT%
exit /b 0
