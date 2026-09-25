@echo off
rem Size-optimized release build with MinGW g++ (32-bit, runs on Windows XP+).
setlocal
cd /d "%~dp0"
if not exist build mkdir build

set DEFINES=-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DNDEBUG
set CXXFLAGS=-std=c++14 -Os -Wall %DEFINES% -fno-exceptions -fno-rtti ^
 -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-threadsafe-statics ^
 -ffunction-sections -fdata-sections -fomit-frame-pointer
set LDFLAGS=-mwindows -static -static-libgcc -static-libstdc++ -s ^
 -Wl,--gc-sections -lws2_32 -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lkernel32

windres --include-dir res -O coff res\feodals.rc -o build\feodals_res.o
if errorlevel 1 exit /b 1

g++ %CXXFLAGS% src\GameState.cpp src\Renderer.cpp src\InputHandler.cpp ^
 src\SaveManager.cpp src\UiCommon.cpp src\NewGameDialog.cpp src\main.cpp ^
 src\DialogKit.cpp src\NetDialogs.cpp src\NetGame.cpp src\Bot.cpp src\Lang.cpp ^
 build\feodals_res.o -o build\Feodals.exe %LDFLAGS%
if errorlevel 1 exit /b 1

strip --strip-all build\Feodals.exe
for %%F in (build\Feodals.exe) do echo Built %%F (%%~zF bytes)
