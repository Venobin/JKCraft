@echo off
setlocal
title JKCraft Playtest
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-JKCraft-Playtest.ps1"
if errorlevel 1 pause

