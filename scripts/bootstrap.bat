@echo off
set TYPE=%1
if "%TYPE%"=="" set TYPE=Release

set PRESET=
set BUILDDIR=

rem Detect OS (basic check)
ver | findstr /i "Windows" >nul
if %errorlevel%==0 (
  rem On Windows, use VS presets
  if "%TYPE%"=="Debug" (
    set PRESET=win-debug
    set BUILDDIR=build\win-debug
  ) else (
    set PRESET=win-release
    set BUILDDIR=build\win-release
  )
) else (
  rem On Linux/macOS, use GCC presets
  if "%TYPE%"=="Debug" (
    set PRESET=dev-debug
    set BUILDDIR=build-debug
  ) else (
    set PRESET=dev
    set BUILDDIR=build
  )
)

echo ==> Configuring (%TYPE%) with preset %PRESET%
cmake --preset %PRESET%

echo ==> Building
cmake --build --preset %PRESET% --parallel

echo ==> Testing
where ctest >nul 2>nul
if errorlevel 1 (
    echo ctest not found, using cmake --build --target test
    cmake --build --preset %PRESET% --target test --config %TYPE%
) else (
    ctest --preset %PRESET% -C %TYPE%
)


echo All good