@echo off
rem Builds and runs the console tests of the game core and the network code.
setlocal
cd /d "%~dp0.."
if not exist build mkdir build
set FLAGS=-std=c++14 -O2 -Wall -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501
set LIBS=-static -static-libgcc -static-libstdc++ -lws2_32 -lcomdlg32 -lgdi32

g++ %FLAGS% tests\test_game.cpp src\GameState.cpp src\SaveManager.cpp ^
  -o build\test_game.exe %LIBS%
if errorlevel 1 exit /b 1
g++ %FLAGS% tests\test_net.cpp src\NetGame.cpp src\GameState.cpp ^
  src\SaveManager.cpp src\UiCommon.cpp -o build\test_net.exe %LIBS%
if errorlevel 1 exit /b 1

build\test_game.exe
if errorlevel 1 exit /b 1
build\test_net.exe
