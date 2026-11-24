@echo off
rem Carnage Heart: build the Android app (CarnageHeart.apk) from your installed game.
rem Run CarnageHeart_Recompiled.exe first so the game is installed on this PC.
setlocal
cd /d "%~dp0"
title Carnage Heart - Build Android App

set "PYEXE="
if exist "%LOCALAPPDATA%\retcomm\toolchains\cmake-clang-v1\current\python\python.exe" set "PYEXE=%LOCALAPPDATA%\retcomm\toolchains\cmake-clang-v1\current\python\python.exe"
if not defined PYEXE for /d %%D in ("%LOCALAPPDATA%\retcomm\toolchains\cmake-clang-v1\*") do if exist "%%~D\python\python.exe" set "PYEXE=%%~D\python\python.exe"
if not defined PYEXE for /d %%D in ("%LOCALAPPDATA%\psxrecomp\toolchains\cmake-clang-v1\*") do if exist "%%~D\python\python.exe" set "PYEXE=%%~D\python\python.exe"
if not defined PYEXE for /f "delims=" %%P in ('where python 2^>nul') do if not defined PYEXE set "PYEXE=%%P"
if not defined PYEXE (
  echo Could not find Python.
  echo Run CarnageHeart_Recompiled.exe once first: it installs the game and its tools.
  pause
  exit /b 1
)

"%PYEXE%" "scripts\android\build_android_app.py" %*
set RC=%ERRORLEVEL%
echo.
if %RC%==0 (echo Finished.) else (echo Something went wrong - see the messages above.)
pause
exit /b %RC%
