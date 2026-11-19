@echo off
REM Sets VER from app\CMakeLists.txt: project(DynamicIslandLux VERSION x.y.z ...)
REM Safe to call from any cwd; path is relative to this script.
REM Usage: call scripts\_version.cmd   OR   call "%~dp0_version.cmd"
set "VER="
set "_LUX_CMAKE=%~dp0..\app\CMakeLists.txt"
if not exist "%_LUX_CMAKE%" (
  echo ERROR: missing "%_LUX_CMAKE%"
  set "_LUX_CMAKE="
  exit /b 1
)
for /f "usebackq tokens=3" %%V in (`findstr /C:"project(DynamicIslandLux VERSION" "%_LUX_CMAKE%"`) do (
  set "VER=%%V"
)
set "_LUX_CMAKE="
if not defined VER (
  echo ERROR: could not parse VERSION from app\CMakeLists.txt
  exit /b 1
)
REM Reject placeholders / junk (expect digits.digits.digits, optional fourth .digits).
echo %VER%| findstr /R /C:"^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" /C:"^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul
if errorlevel 1 (
  echo ERROR: invalid VERSION "%VER%" ^(expected x.y.z^)
  set "VER="
  exit /b 1
)
exit /b 0
