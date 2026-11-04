@echo off
REM Strict release gate: build+smoke → package assets → print tag command.
REM Do NOT tag before this script exits 0. Do NOT run make-release-assets before build.
setlocal EnableExtensions
cd /d "%~dp0\.."

echo ========== 1/2 app\build.bat (Release + smoke) ==========
call app\build.bat
if errorlevel 1 (
  echo FAILED: build/smoke — not tagging
  exit /b %ERRORLEVEL%
)

echo.
echo ========== 2/2 scripts\make-release-assets.bat ==========
call scripts\make-release-assets.bat
if errorlevel 1 (
  echo FAILED: release assets / VERSIONINFO — not tagging
  exit /b %ERRORLEVEL%
)

echo.
echo ========== READY TO TAG ==========
for /f %%H in ('git rev-parse HEAD') do set HASH=%%H
echo Commit: %HASH%
echo.
echo After cold-start checklist passes, run:
echo   git tag -a v1.0.0 -m "Dynamic Island Lux standalone v1.0.0"
echo   git push origin v1.0.0
echo.
echo Then edit RELEASE_NOTES.md date + paste tag hash, re-run make-release-assets.bat
echo ONLY if notes changed (that creates a NEW commit — retag or amend notes before first push).
echo.
echo Attach dist\v1.0.0\* to the GitHub Release.
exit /b 0
