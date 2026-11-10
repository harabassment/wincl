@echo off
setlocal
cd /d "%~dp0"
where zig >nul 2>nul
if errorlevel 1 (
 echo Install Zig 0.13.0 from https://ziglang.org/download/ and add it to PATH.
 pause
 exit /b 1
)
zig rc /fo app.res app.rc
if errorlevel 1 exit /b 1
zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -static -s -municode main.cpp app.res -o CleanPC.exe -lcomctl32 -lshell32 -lole32 -luuid -ladvapi32 -lcomdlg32 -luxtheme -lgdi32 -luser32 -Wl,--subsystem,windows -Wl,--nxcompat -Wl,--dynamicbase
if errorlevel 1 exit /b 1
echo Built CleanPC.exe
pause
