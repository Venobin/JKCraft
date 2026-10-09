@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0Runtime\Java25\bin\javaw.exe" (
  echo JKCraft: Java 25 runtime is missing.
  pause
  exit /b 1
)
start "" /D "%~dp0" "%~dp0Runtime\Java25\bin\javaw.exe" -jar "%~dp0JKCraft-Launcher.jar"
exit /b
