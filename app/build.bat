@echo off
setlocal
cd /d "%~dp0\.."

where cmake >nul 2>&1
if errorlevel 1 (
  echo CMake not found in PATH. Install CMake and Visual Studio 2022 C++ tools.
  exit /b 1
)

cmake -S app -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1

set EXE=build\Release\DynamicIslandLux.exe
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
exit /b %ERRORLEVEL%
