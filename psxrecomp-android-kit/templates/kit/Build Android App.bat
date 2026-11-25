@echo off
rem Build the Android app (APK) from your installed game (PSXRecomp Android kit).
rem Run the game's setup first so the game is installed on this PC.
rem Copy this file to the root of the game's setup kit.
setlocal
cd /d "%~dp0"
title Build Android App

set "PYEXE="
if exist "%LOCALAPPDATA%\retcomm\toolchains\cmake-clang-v1\current\python\python.exe" set "PYEXE=%LOCALAPPDATA%\retcomm\toolchains\cmake-clang-v1\current\python\python.exe"
if not defined PYEXE for /d %%D in ("%LOCALAPPDATA%\retcomm\toolchains\cmake-clang-v1\*") do if exist "%%~D\python\python.exe" set "PYEXE=%%~D\python\python.exe"
if not defined PYEXE for /d %%D in ("%LOCALAPPDATA%\psxrecomp\toolchains\cmake-clang-v1\*") do if exist "%%~D\python\python.exe" set "PYEXE=%%~D\python\python.exe"
if not defined PYEXE for /f "delims=" %%P in ('where python 2^>nul') do if not defined PYEXE set "PYEXE=%%P"
if not defined PYEXE (
  echo Could not find Python.
  echo Run the game's setup once first: it installs the game and its tools.
  pause
  exit /b 1
)

"%PYEXE%" "psxrecomp-android-kit\scripts\build_android_app.py" %*
set RC=%ERRORLEVEL%
echo.
if %RC%==0 (echo Finished.) else (echo Something went wrong - see the messages above.)
pause
exit /b %RC%
