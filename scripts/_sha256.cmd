@echo off
REM Usage: call "%~dp0_sha256.cmd" "path\to\file"
REM Sets SHA256_HEX to 64 lowercase hex chars, or clears it and exits 1 on failure.
set "SHA256_HEX="
if "%~1"=="" exit /b 1
if not exist "%~1" exit /b 1

set "_lux_sha_out=%TEMP%\lux-sha256-%RANDOM%%RANDOM%.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_sha256.ps1" -Path "%~1" -OutFile "%_lux_sha_out%"
if errorlevel 1 (
  del "%_lux_sha_out%" 2>nul
  exit /b 1
)
if not exist "%_lux_sha_out%" exit /b 1

set /p SHA256_HEX=<"%_lux_sha_out%"
del "%_lux_sha_out%" 2>nul

if not defined SHA256_HEX exit /b 1
REM Exactly 64 hex chars (ps1 already validated length + charset).
if "%SHA256_HEX:~63,1%"=="" exit /b 1
if not "%SHA256_HEX:~64,1%"=="" (
  set "SHA256_HEX="
  exit /b 1
)
exit /b 0
