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

rem Fail before CMake/MSBuild emits a long, misleading list of missing standard headers.
rem Nested wildcards need nested for /d loops (cmd does not expand VS\*\*\VC\Tools\MSVC\*).
set "HAS_WINDOWS_SDK="
for /d %%I in ("%ProgramFiles(x86)%\Windows Kits\10\Include\*") do (
  if exist "%%~fI\um\windows.h" set "HAS_WINDOWS_SDK=1"
)
if not defined HAS_WINDOWS_SDK (
  echo ERROR: Windows 10/11 SDK headers were not found.
  echo Install the "Windows 10 SDK" or "Windows 11 SDK" component through Visual Studio Installer,
  echo then rerun this script.
  exit /b 1
)

set "HAS_MSVC_CRT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    for /d %%T in ("%%I\VC\Tools\MSVC\*") do (
      if exist "%%~fT\include\vcruntime.h" set "HAS_MSVC_CRT=1"
    )
  )
)
if not defined HAS_MSVC_CRT (
  rem VS 2022 lives under ...\2022\<edition>, VS 2026 under ...\18\<edition>.
  for %%R in ("%ProgramFiles%\Microsoft Visual Studio" "%ProgramFiles(x86)%\Microsoft Visual Studio") do (
    for /d %%Y in ("%%~R\*") do (
      for /d %%E in ("%%~fY\*") do (
        for /d %%I in ("%%~fE\VC\Tools\MSVC\*") do (
          rem crtdbg.h lives in Universal CRT (Windows Kits\...\ucrt); MSVC ships vcruntime.h.
          if exist "%%~fI\include\vcruntime.h" set "HAS_MSVC_CRT=1"
          if exist "%%~fI\include\crtdbg.h" set "HAS_MSVC_CRT=1"
        )
      )
    )
  )
)
if not defined HAS_MSVC_CRT (
  echo ERROR: MSVC C++ headers were not found.
  echo In Visual Studio Installer, add "Desktop development with C++" and the MSVC build tools,
  echo then rerun this script.
  exit /b 1
)

rem Ask a running copy to quit (WM_CLOSE on the host window) before taskkill.
powershell -NoProfile -Command "Add-Type -MemberDefinition '[DllImport(\"user32.dll\", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string w); [DllImport(\"user32.dll\")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);' -Name LuxClose -Namespace DynIsland -ErrorAction SilentlyContinue; $h = [DynIsland.LuxClose]::FindWindow('DynamicIslandLux.Host', $null); if ($h -ne [IntPtr]::Zero) { [DynIsland.LuxClose]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }"
ping -n 3 127.0.0.1 >nul
tasklist /FI "IMAGENAME eq DynamicIslandLux.exe" | find /I "DynamicIslandLux.exe" >nul
if not errorlevel 1 (
  taskkill /IM DynamicIslandLux.exe >nul 2>&1
  ping -n 2 127.0.0.1 >nul
)
tasklist /FI "IMAGENAME eq DynamicIslandLux.exe" | find /I "DynamicIslandLux.exe" >nul
if not errorlevel 1 (
  echo WARNING: DynamicIslandLux.exe did not exit; forcing taskkill /F
  taskkill /F /IM DynamicIslandLux.exe >nul 2>&1
)
rem ping instead of timeout: timeout prints "Input redirection is not supported" under redirected stdin.
ping -n 2 127.0.0.1 >nul

rem Match the generator to the newest VS with the C++ toolset.
rem 17 = Visual Studio 2022, 18 = Visual Studio 2026 (needs CMake 4.2+).
rem Any other major (or vswhere missing) lets CMake pick with -A x64.
set "VS_VER="
set "VS_MAJOR="
set "CMAKE_GEN="
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do (
    if not defined VS_VER set "VS_VER=%%V"
  )
)
for /f "tokens=1 delims=." %%M in ("%VS_VER%") do set "VS_MAJOR=%%M"
if "%VS_MAJOR%"=="17" set "CMAKE_GEN=Visual Studio 17 2022"
if "%VS_MAJOR%"=="18" set "CMAKE_GEN=Visual Studio 18 2026"
if "%VS_MAJOR%"=="18" echo NOTE: Visual Studio 2026 requires CMake 4.2 or newer.
if defined CMAKE_GEN (
  echo CMake generator: %CMAKE_GEN% ^(Visual Studio %VS_VER%^)
  "%CMAKE%" -S app -B build -G "%CMAKE_GEN%" -A x64
) else (
  echo CMake generator: default Visual Studio ^(-A x64^)
  if defined VS_VER echo Visual Studio %VS_VER%
  "%CMAKE%" -S app -B build -A x64
)
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
