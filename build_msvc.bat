@echo off
rem Size-optimized release build with MSVC cl.exe (32-bit).
rem Run from a "Developer Command Prompt" or let the script find vcvars32.bat.
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto have_cl
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_cl
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%I"
if not defined VSDIR goto no_cl
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul
where cl >nul 2>nul
if not errorlevel 1 goto have_cl
:no_cl
echo cl.exe not found. Install Visual Studio Build Tools with the C++ workload.
exit /b 1
:have_cl

if not exist build\msvc mkdir build\msvc

set DEFINES=/DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0501 /DWINVER=0x0501 /DNDEBUG
rem /O1 = minimize size (includes /Os), /GS- no stack cookies, /GR- no RTTI,
rem /EHs-c- no exceptions, /MT static CRT (no redistributable needed),
rem /Gy /Gw let the linker drop unused functions and data.
set CFLAGS=/nologo /W3 /O1 /GS- /GR- /EHs-c- /MT /Gy /Gw /utf-8 %DEFINES%
rem For Windows XP use the v141_xp toolset and /SUBSYSTEM:WINDOWS,5.01;
rem newer toolsets produce binaries that need Windows Vista/7 or later.
set LFLAGS=/nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /INCREMENTAL:NO /MANIFEST:NO /DEBUG:NONE

rc /nologo /fo build\msvc\feodals.res res\feodals.rc
if errorlevel 1 exit /b 1

cl %CFLAGS% /Fobuild\msvc\ /Febuild\Feodals_msvc.exe ^
 src\GameState.cpp src\Renderer.cpp src\InputHandler.cpp src\SaveManager.cpp ^
 src\UiCommon.cpp src\NewGameDialog.cpp src\main.cpp src\DialogKit.cpp ^
 src\NetDialogs.cpp src\NetGame.cpp build\msvc\feodals.res ^
 /link %LFLAGS% user32.lib gdi32.lib comdlg32.lib comctl32.lib ws2_32.lib kernel32.lib
if errorlevel 1 exit /b 1

for %%F in (build\Feodals_msvc.exe) do echo Built %%F (%%~zF bytes)
