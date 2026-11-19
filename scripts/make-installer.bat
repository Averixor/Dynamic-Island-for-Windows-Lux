@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\.."

set VER=1.0.5
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

REM Keep this as one cmd line: bare "|" inside caret-continued powershell splits the
REM batch command and Windows then tries to "Open with" the leftover "Write-Host".
powershell -NoProfile -Command "$setup='%~dp0..\dist\v%VER%\DynamicIslandLux-Setup-%VER%.exe'; $sums='%~dp0..\dist\v%VER%\SHA256SUMS.txt'; $h=(Get-FileHash -Algorithm SHA256 -LiteralPath $setup).Hash.ToLowerInvariant(); $line=$h + '  DynamicIslandLux-Setup-%VER%.exe'; if (Test-Path -LiteralPath $sums) { $kept=@(); foreach ($row in Get-Content -LiteralPath $sums) { if ($row -notmatch 'DynamicIslandLux-Setup-') { $kept += $row } }; Set-Content -Encoding ascii -LiteralPath $sums -Value ($kept + $line) } else { Set-Content -Encoding ascii -LiteralPath $sums -Value $line }; Write-Output $line"

echo.
echo OK - installer: %OUT%
exit /b 0
