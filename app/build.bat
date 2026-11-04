@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

set "CMAKE="
where cmake >nul 2>&1
if not errorlevel 1 (
  for /f "delims=" %%I in ('where cmake') do (
    if not defined CMAKE set "CMAKE=%%I"
  )
)

if not defined CMAKE (
  if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles%\CMake\bin\cmake.exe"
)
if not defined CMAKE (
  set "PF86=%ProgramFiles(x86)%"
  if exist "%PF86%\CMake\bin\cmake.exe" set "CMAKE=%PF86%\CMake\bin\cmake.exe"
)
if not defined CMAKE (
  if exist "%LocalAppData%\Programs\CMake\bin\cmake.exe" set "CMAKE=%LocalAppData%\Programs\CMake\bin\cmake.exe"
)
if not defined CMAKE (
  echo CMake not found. Install CMake and/or add it to PATH.
  echo Tried: PATH, ProgramFiles\CMake\bin, ProgramFiles^(x86^)\CMake\bin, LocalAppData\Programs\CMake\bin
  exit /b 1
)
echo Using CMake: %CMAKE%

"%CMAKE%" -S app -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
"%CMAKE%" --build build --config Release
if errorlevel 1 exit /b 1

set EXE=build\Release\DynamicIslandLux.exe
if not exist "%EXE%" (
  echo ERROR: %EXE% missing after build
  exit /b 1
)
echo.
echo Built: %EXE%
echo === smoke --no-workers ===
"%EXE%" --smoke-test --no-workers
if errorlevel 1 exit /b %ERRORLEVEL%
echo === smoke schema backdrop (no --backdrop) ===
"%EXE%" --smoke-test
if errorlevel 1 exit /b %ERRORLEVEL%
echo === smoke --backdrop=blur ===
"%EXE%" --smoke-test --backdrop=blur
if errorlevel 1 exit /b %ERRORLEVEL%
echo === smoke --backdrop=acrylic ===
"%EXE%" --smoke-test --backdrop=acrylic
if errorlevel 1 exit /b %ERRORLEVEL%
echo === smoke --test-json ===
"%EXE%" --test-json
if errorlevel 1 exit /b %ERRORLEVEL%
echo.
echo build.bat OK - next: scripts\make-release-assets.bat  (or scripts\release.bat)
exit /b 0
