@echo off
REM Strict release gate: build+smoke -> package assets -> print tag command.
REM Do NOT tag before this script exits 0. Do NOT run make-release-assets before build.
setlocal EnableExtensions
cd /d "%~dp0\.."
call "%~dp0_version.cmd"
if errorlevel 1 exit /b 1

echo ========== preflight ==========
git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
  echo FAILED: not a git checkout
  exit /b 1
)
for /f "delims=" %%B in ('git rev-parse --abbrev-ref HEAD') do set "BRANCH=%%B"
if /I not "%BRANCH%"=="main" (
  echo FAILED: release.bat must run on main ^(current: %BRANCH%^)
  exit /b 1
)
git diff --quiet
if errorlevel 1 (
  echo FAILED: working tree has unstaged changes. Commit notes and version first.
  exit /b 1
)
git diff --cached --quiet
if errorlevel 1 (
  echo FAILED: index has staged changes. Commit notes and version first.
  exit /b 1
)
git rev-parse -q --verify "refs/tags/v%VER%" >nul 2>&1
if not errorlevel 1 (
  echo FAILED: tag v%VER% already exists
  exit /b 1
)
findstr /C:"v%VER%" RELEASE_NOTES.md >nul
if errorlevel 1 (
  echo FAILED: RELEASE_NOTES.md does not mention v%VER%. Edit the notes before tagging.
  exit /b 1
)

echo ========== 1/2 app\build.bat (Release + smoke) ==========
call app\build.bat
if errorlevel 1 (
  echo FAILED: build/smoke - not tagging
  exit /b %ERRORLEVEL%
)

echo.
echo ========== 2/2 scripts\make-release-assets.bat ==========
set LUX_REQUIRE_INSTALLER=1
call scripts\make-release-assets.bat
if errorlevel 1 (
  echo FAILED: release assets / VERSIONINFO - not tagging
  exit /b %ERRORLEVEL%
)

echo.
echo ========== READY TO TAG ==========
for /f %%H in ('git rev-parse HEAD') do set HASH=%%H
echo Version: %VER%
echo Commit: %HASH%
echo.
echo Notes and the CMake version must already be committed. Do not edit RELEASE_NOTES.md after tagging.
echo After the cold-start checklist passes, run:
echo   git tag -a v%VER% %HASH% -m "Dynamic Island Lux standalone v%VER%"
echo   git push origin v%VER%
echo.
echo Attach dist\v%VER%\* to the GitHub Release. The tag, not a hash in the notes, identifies the commit.
exit /b 0
