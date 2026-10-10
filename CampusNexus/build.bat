@echo off
rem ---------------------------------------------------------------
rem  Campus Nexus - one-click build for Windows (no CMake needed)
rem  Uses g++ (MinGW-w64 / MSYS2 / WinLibs) if found, otherwise MSVC (cl).
rem  For MSVC run this from "x64 Native Tools Command Prompt for VS".
rem ---------------------------------------------------------------
setlocal
cd /d "%~dp0"
if not exist bin mkdir bin

where g++ >nul 2>nul
if %errorlevel%==0 goto gcc
where cl >nul 2>nul
if %errorlevel%==0 goto msvc

echo.
echo No C++ compiler found on PATH.
echo Install ONE of these, then run build.bat again:
echo   - Visual Studio 2022 Community ("Desktop development with C++")
echo   - MSYS2 or WinLibs (MinGW-w64 g++ 9 or newer) and add its bin folder to PATH
exit /b 1

:gcc
echo Building with g++ (static, so the .exe needs no extra DLLs) ...
g++ -std=c++17 -O2 -Wall -Wextra -static -Iinclude src\campus_map.cpp src\visualizer.cpp src\main.cpp -o bin\campus_nexus.exe
if errorlevel 1 goto fail
g++ -std=c++17 -O2 -Wall -Wextra -static -Iinclude src\campus_map.cpp tests\test_campus.cpp -o bin\campus_tests.exe
if errorlevel 1 goto fail
goto ok

:msvc
echo Building with MSVC ...
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /Iinclude src\campus_map.cpp src\visualizer.cpp src\main.cpp /Fobin\ /Febin\campus_nexus.exe
if errorlevel 1 goto fail
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /Iinclude src\campus_map.cpp tests\test_campus.cpp /Fobin\ /Febin\campus_tests.exe
if errorlevel 1 goto fail
goto ok

:ok
echo.
echo Build finished. Running the tests ...
bin\campus_tests.exe
if errorlevel 1 goto fail
echo.
echo All good. Start the program with run.bat
exit /b 0

:fail
echo.
echo Build or tests FAILED.
exit /b 1
