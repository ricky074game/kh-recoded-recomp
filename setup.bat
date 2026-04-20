@echo off
setlocal enabledelayedexpansion

:: setup.bat - Kingdom Hearts Re:coded Static Recompilation Build Orchestrator
:: This script extracts the ROM, runs the lifter, and compiles the final executable.

if "%~1"=="" (
    echo Usage: setup.bat ^<path_to_nds_rom^>
    exit /b 1
)

set ROM_PATH=%~1

if not exist "%ROM_PATH%" (
    echo Error: ROM file not found at %ROM_PATH%
    exit /b 1
)

:: Known good dump SHA-256 for US version
set KNOWN_SHA256=a93deee92eef8e05c86d8b376c28f114c0a4e760c6d997cc0f69a19bbfbc624f

echo =^> Validating ROM checksum...
for /f "skip=1 tokens=* delims=" %%# in ('certutil -hashfile "%ROM_PATH%" SHA256') do (
    set ACTUAL_SHA256=%%#
    goto :hash_done
)
:hash_done
set ACTUAL_SHA256=%ACTUAL_SHA256: =%
set ACTUAL_SHA256=%ACTUAL_SHA256:	=%

echo Calculated SHA-256: %ACTUAL_SHA256%
if /I not "%ACTUAL_SHA256%"=="%KNOWN_SHA256%" (
    echo Warning: Checksum does not match the known good US dump.
    echo Expected: %KNOWN_SHA256%
    echo Proceeding anyway, but compilation or runtime errors may occur.
) else (
    echo Checksum matches known good dump. Proceeding.
)

:: Step 1: Extract ROM via ndstool
echo =^> Step 1: Extracting ROM...
if not exist recoded mkdir recoded
cd recoded

set NDSTOOL=..\tools\nds_extractor\ndstool.exe
if not exist "%NDSTOOL%" (
    set NDSTOOL=ndstool.exe
)

%NDSTOOL% -x "..\!ROM_PATH!" -9 arm9.bin -7 arm7.bin -y9 y9.bin -y7 y7.bin -d data -y overlay -t banner.bin -h header.bin
cd ..

:: Step 2: Run lifter on arm9.bin + overlays
echo =^> Step 2: Running Capstone lifter...
if not exist generated mkdir generated
echo =^> Building lifter...
if not exist build_lifter mkdir build_lifter
cd build_lifter
cmake ..\lifter
cmake --build . --config Release
cd ..

echo =^> Executing lifter...
build_lifter\Release\lifter_engine.exe recoded\arm9.bin generated\arm9_translated.cpp 0x02000000

:: Step 3: Invoke CMake to compile generated code + runtime + mod_api
echo =^> Step 3: Compiling generated code, runtime, and mod_api...
if not exist build mkdir build
cd build
cmake ..
cmake --build . --config Release
cd ..

:: Step 4: Output final executable
echo =^> Step 4: Compilation complete.
echo The final executable is located in the build directory.
echo You can now run it to play the game.
