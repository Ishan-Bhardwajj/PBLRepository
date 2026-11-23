@echo off
rem Starts Campus Nexus (builds it first if needed). Extra arguments are passed through,
rem e.g.  run.bat --path "Main Gate" Library
setlocal
cd /d "%~dp0"
if not exist bin\campus_nexus.exe call build.bat
if not exist bin\campus_nexus.exe exit /b 1
bin\campus_nexus.exe %*
if "%~1"=="" pause
