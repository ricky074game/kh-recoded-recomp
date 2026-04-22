@echo off
REM setup.bat - Kingdom Hearts Re:coded Static Recompilation Build Orchestrator
REM Completely automatic: installs dependencies silently and compiles the project
REM Usage: setup.bat [options] <path_to_nds_rom>

setlocal enabledelayedexpansion

if "%~1"=="" (
    echo Usage: setup.bat [options] ^<path_to_nds_rom^>
    echo.
    echo Options:
    echo   -d, --debug           Build runtime in Debug mode
    echo   -j, --jobs N          Max parallel compile jobs
    echo   --skip-deps           Skip automatic dependency installation
    exit /b 1
)

set "ROM_PATH=%~1"
set "DEBUG_MODE=0"
set "SKIP_DEPS=0"
set "BUILD_JOBS="

:parse_args
if "%~1"=="" goto args_done
if "%~1"=="-d" goto debug_mode
if "%~1"=="--debug" goto debug_mode
if "%~1"=="-j" goto jobs_mode
if "%~1"=="--jobs" goto jobs_mode
if "%~1"=="--skip-deps" (
    set "SKIP_DEPS=1"
    shift
    goto parse_args
) else if "%~1"=="-h" goto show_help
else if "%~1"=="--help" goto show_help
else (
    shift
    goto parse_args
)

:debug_mode
set "DEBUG_MODE=1"
shift
goto parse_args

:jobs_mode
set "BUILD_JOBS=%~2"
shift
shift
goto parse_args

:show_help
echo Usage: setup.bat [options] ^<path_to_nds_rom^>
echo.
echo Options:
echo   -d, --debug           Build runtime in Debug mode
echo   -j, --jobs N          Max parallel compile jobs
echo   --skip-deps           Skip automatic dependency installation
exit /b 0

:args_done
if not exist "%ROM_PATH%" (
    echo [ERROR] ROM file not found: %ROM_PATH%
    exit /b 1
)

REM Install dependencies unless --skip-deps was specified
if %SKIP_DEPS% equ 0 (
    call :install_dependencies_silent
)

REM Validate ROM checksum
set "KNOWN_SHA256=a93deee92eef8e05c86d8b376c28f114c0a4e760c6d997cc0f69a19bbfbc624f"
echo [*] Validating ROM checksum...
for /f "skip=1 tokens=* delims=" %%# in ('certutil -hashfile "%ROM_PATH%" SHA256') do (
    set "ACTUAL_SHA256=%%#"
    goto :hash_done
)
:hash_done
set "ACTUAL_SHA256=!ACTUAL_SHA256: =!"
set "ACTUAL_SHA256=!ACTUAL_SHA256:=!"

if /I not "!ACTUAL_SHA256!"=="!KNOWN_SHA256!" (
    echo [WARNING] ROM checksum mismatch. Expected: !KNOWN_SHA256! Got: !ACTUAL_SHA256!
)

REM Step 0: Build lifter
echo [*] Building Lifter Engine...
if not exist build_lifter mkdir build_lifter
cd build_lifter
cmake ..\lifter -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 17 2022" -A x64 >nul 2>&1
if errorlevel 1 cmake ..\lifter -DCMAKE_BUILD_TYPE=Release >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake configuration failed
    cd ..
    exit /b 1
)
cmake --build . --config Release --parallel %NUMBER_OF_PROCESSORS% >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Lifter build failed
    cd ..
    exit /b 1
)
cd ..
echo [*] Lifter Engine built successfully

REM Step 1: Extract ROM via ndstool
echo [*] Extracting ROM...
if not exist recoded mkdir recoded
cd recoded
set "NDSTOOL=..\tools\nds_extractor\ndstool.exe"
if not exist "!NDSTOOL!" set "NDSTOOL=ndstool.exe"
"!NDSTOOL!" -x "..\!ROM_PATH!" -9 arm9.bin -7 arm7.bin -y9 y9.bin -y7 y7.bin -d data -y overlay -t banner.bin -h header.bin >nul 2>&1
if errorlevel 1 (
    echo [ERROR] ROM extraction failed
    cd ..
    exit /b 1
)
cd ..
echo [*] ROM extracted successfully

REM Step 2: Run lifter on arm9.bin
echo [*] Running lifter on arm9.bin...
if not exist "runtime\src\generated" mkdir "runtime\src\generated"
build_lifter\Release\lifter_engine.exe recoded\arm9.bin "runtime\src\generated\arm9_translated.cpp" 0x02000000 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Lifter execution failed
    exit /b 1
)
echo [*] Lifter completed successfully

REM Step 3: Compile with CMake
echo [*] Configuring CMake...
if not exist build mkdir build
cd build

REM Find Qt6 and set CMAKE_PREFIX_PATH
set "CMAKE_PREFIX_PATH="
for /d %%D in (C:\Qt\*) do (
    if exist "%%D\6.?.*\msvc2022_64\lib\cmake\Qt6" (
        set "CMAKE_PREFIX_PATH=%%D\6.?.*\msvc2022_64\lib\cmake"
        goto qt_found
    )
)
for /d %%D in (C:\Qt\*) do (
    if exist "%%D\6.*\lib\cmake\Qt6" (
        set "CMAKE_PREFIX_PATH=%%D\6.*\lib\cmake"
        goto qt_found
    )
)

:qt_found
if "%DEBUG_MODE%"=="1" (
    cmake .. -DCMAKE_BUILD_TYPE=Debug -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="!CMAKE_PREFIX_PATH!" >nul 2>&1
    if errorlevel 1 cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="!CMAKE_PREFIX_PATH!" >nul 2>&1
) else (
    cmake .. -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="!CMAKE_PREFIX_PATH!" >nul 2>&1
    if errorlevel 1 cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="!CMAKE_PREFIX_PATH!" >nul 2>&1
)

if errorlevel 1 (
    echo [ERROR] CMake configuration failed
    cd ..
    exit /b 1
)

echo [*] Building project...
if "%BUILD_JOBS%"=="" (
    cmake --build . --config Release --parallel %NUMBER_OF_PROCESSORS% >nul 2>&1
) else (
    cmake --build . --config Release --parallel %BUILD_JOBS% >nul 2>&1
)

if errorlevel 1 (
    echo [ERROR] Build failed
    cd ..
    exit /b 1
)
cd ..

echo.
echo ============================================================
echo [SUCCESS] Build completed!
echo [*] Executable: build\runtime\recoded.exe
echo ============================================================

exit /b 0

REM ===================================================================
REM Completely silent dependency installation
REM ===================================================================
:install_dependencies_silent
echo [*] Checking and installing dependencies silently...

REM Check and install CMake silently
cmake --version >nul 2>&1
if errorlevel 1 (
    echo [*] Installing CMake...
    REM Try Winget (silent)
    winget install --silent --accept-source-agreements Kitware.CMake >nul 2>&1
    if errorlevel 1 (
        REM Try Chocolatey (silent)
        choco install -y cmake --no-progress >nul 2>&1
    )
)

REM Check for Visual Studio Build Tools
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo [*] Visual Studio Build Tools not found, attempting to install...
    REM Try to install Build Tools silently via Winget
    winget install --silent --accept-source-agreements Microsoft.VisualStudio.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools" >nul 2>&1
)

REM Find and configure Qt6
set "QT6_FOUND=0"
for /d %%D in (C:\Qt\*) do (
    if exist "%%D\6.?.*\msvc2022_64\lib\cmake\Qt6" (
        set "QT6_FOUND=1"
        goto qt_check_done
    )
)
for /d %%D in (C:\Qt\*) do (
    if exist "%%D\6.*\lib\cmake\Qt6" (
        set "QT6_FOUND=1"
        goto qt_check_done
    )
)

:qt_check_done
if %QT6_FOUND% equ 0 (
    echo [*] Installing Qt6 (this may take a few minutes)...
    REM Try via winget
    winget install --silent --accept-source-agreements Qt.Qt >nul 2>&1
    if errorlevel 1 (
        echo [*] Qt6 Winget install failed, attempting PowerShell download...
        powershell -Command "try { (New-Object System.Net.WebClient).DownloadFile('https://download.qt.io/official_releases/online_installers/qt-online-installer-windows.exe', \"$env:TEMP\qt-installer.exe\"); Write-Host '[*] Qt6 installer downloaded to: %TEMP%\qt-installer.exe'; Write-Host '[*] Please run manually if needed' } catch { Write-Host '[!] Could not download Qt6. Please install manually from https://qt.io' }" >nul 2>&1
    )
)

REM Find and configure Vulkan SDK
if not exist "C:\VulkanSDK" (
    echo [*] Installing Vulkan SDK...
    REM Try Winget silent install
    winget install --silent --accept-source-agreements LunarG.VulkanSDK >nul 2>&1
    if errorlevel 1 (
        REM Try PowerShell download
        powershell -Command "try { (New-Object System.Net.WebClient).DownloadFile('https://sdk.lunarg.com/sdk/download/latest/windows/vulkan-sdk-latest-windows.exe', \"$env:TEMP\vulkan-installer.exe\"); Write-Host '[*] Vulkan installer downloaded'; cmd /c \"$env:TEMP\vulkan-installer.exe\" /S } catch { Write-Host '[!] Could not download Vulkan SDK' }" >nul 2>&1
    )
)

echo [*] Dependencies ready
exit /b 0
