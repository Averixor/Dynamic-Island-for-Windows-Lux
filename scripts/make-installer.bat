@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\.."

call "%~dp0_version.cmd"
if errorlevel 1 exit /b 1

set EXE=build\Release\Dynamic Island Lux.exe
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
echo Version: %VER%
"%ISCC%" "/DMyAppVersion=%VER%" "%ISS%"
if errorlevel 1 (
  echo FAILED: Inno Setup compile
  exit /b %ERRORLEVEL%
)

if not exist "%OUT%" (
  echo FAILED: expected output missing: %OUT%
  exit /b 1
)

REM Hash via .NET SHA256 (Get-FileHash is missing in some PowerShell hosts).
call "%~dp0_sha256.cmd" "%OUT%"
if errorlevel 1 (
  echo FAILED: SHA256 of Setup exe
  exit /b 1
)
set "SUMS=dist\v%VER%\SHA256SUMS.txt"
set "SETUP_LINE=%SHA256_HEX%  DynamicIslandLux-Setup-%VER%.exe"
REM Rewrite sums: keep non-Setup lines, append fresh Setup hash.
set "SUMS_TMP=%SUMS%.tmp"
if exist "%SUMS_TMP%" del "%SUMS_TMP%"
if exist "%SUMS%" (
  for /f "usebackq delims=" %%L in ("%SUMS%") do (
    echo %%L| findstr /I /C:"DynamicIslandLux-Setup-" >nul
    if errorlevel 1 >>"%SUMS_TMP%" echo %%L
  )
)
>>"%SUMS_TMP%" echo %SETUP_LINE%
move /Y "%SUMS_TMP%" "%SUMS%" >nul
if errorlevel 1 (
  echo FAILED: could not write SHA256SUMS.txt for Setup
  exit /b 1
)
echo %SETUP_LINE%
findstr /I /C:"DynamicIslandLux-Setup-%VER%.exe" "%SUMS%" >nul
if errorlevel 1 (
  echo FAILED: Setup line missing from SHA256SUMS.txt
  exit /b 1
)

echo.
echo OK - installer: %OUT%
exit /b 0
