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
echo.
echo Built: build\Release\DynamicIslandLux.exe
echo Smoke: build\Release\DynamicIslandLux.exe --smoke-test
endlocal
