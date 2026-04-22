@echo off
setlocal EnableExtensions EnableDelayedExpansion
goto :main

:usage
echo Usage: setup.bat [options] ^<path_to_nds_rom^>
echo.
echo Options:
echo   -d, --debug             Build runtime in Debug mode.
echo   -j, --jobs N            Max parallel compile jobs.
echo       --lift-jobs N       Max overlay lift jobs ^(currently capped at 4^).
echo       --skip-lifter-build Reuse existing lifter binary if still up to date.
echo       --force-extract     Force ROM extraction even when cache is valid.
echo       --force-lift        Force binary lifting even when cache is valid.
echo       --skip-deps         Skip automatic dependency installation.
echo   -h, --help              Show this help text.
exit /b 0

:parse_args
if "%~1"=="" exit /b 0

if "%~1"=="-d" (
    set "DEBUG_MODE=1"
    shift /1
    goto :parse_args
)
if "%~1"=="--debug" (
    set "DEBUG_MODE=1"
    shift /1
    goto :parse_args
)
if "%~1"=="-j" (
    if "%~2"=="" (
        echo [ERROR] --jobs requires a value.
        exit /b 1
    )
    set "BUILD_JOBS=%~2"
    shift /1
    shift /1
    goto :parse_args
)
if "%~1"=="--jobs" (
    if "%~2"=="" (
        echo [ERROR] --jobs requires a value.
        exit /b 1
    )
    set "BUILD_JOBS=%~2"
    shift /1
    shift /1
    goto :parse_args
)
if "%~1"=="--lift-jobs" (
    if "%~2"=="" (
        echo [ERROR] --lift-jobs requires a value.
        exit /b 1
    )
    set "LIFT_JOBS=%~2"
    shift /1
    shift /1
    goto :parse_args
)
if "%~1"=="--skip-lifter-build" (
    set "SKIP_LIFTER_BUILD=1"
    shift /1
    goto :parse_args
)
if "%~1"=="--force-extract" (
    set "FORCE_EXTRACT=1"
    shift /1
    goto :parse_args
)
if "%~1"=="--force-lift" (
    set "FORCE_LIFT=1"
    shift /1
    goto :parse_args
)
if "%~1"=="--skip-deps" (
    set "SKIP_DEPS=1"
    shift /1
    goto :parse_args
)
if "%~1"=="-h" (
    call :usage
    exit /b 0
)
if "%~1"=="--help" (
    call :usage
    exit /b 0
)
if defined ROM_PATH (
    echo [ERROR] Too many positional arguments.
    exit /b 1
)

set "ROM_PATH=%~f1"
shift /1
goto :parse_args

:guard_against_msys2_path
for /f "delims=" %%I in ('where cmake 2^>nul') do (
    echo %%~fI | findstr /I "devkitpro msys2" >nul
    if not errorlevel 1 (
        echo [FATAL] Found MSYS2/devkitPro CMake in PATH: %%~fI
        echo [FATAL] Remove MSYS2/devkitPro build tools from the global Windows PATH and retry.
        exit /b 1
    )
)
exit /b 0

:install_dependencies
echo [*] Checking and installing dependencies...

cmake --version >nul 2>&1
if errorlevel 1 if not exist "C:\Program Files\CMake\bin\cmake.exe" (
    echo [*] Downloading and installing CMake...
    powershell -NoProfile -Command ^
        "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $out = Join-Path $env:TEMP 'cmake.msi'; (New-Object System.Net.WebClient).DownloadFile('https://github.com/Kitware/CMake/releases/download/v3.29.3/cmake-3.29.3-windows-x86_64.msi', $out); Start-Process 'msiexec.exe' -ArgumentList '/i',$out,'/quiet','/norestart','ADD_CMAKE_TO_PATH=System' -Wait -NoNewWindow; exit 0 } catch { Write-Host '[FATAL] Failed to install CMake'; exit 1 }" || exit /b 1
)
if exist "C:\Program Files\CMake\bin\cmake.exe" set "PATH=C:\Program Files\CMake\bin;%PATH%"

where cl.exe >nul 2>&1
if errorlevel 1 (
    set "VSWHERE_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE_PATH!" (
        for /f "usebackq delims=" %%I in (`"!VSWHERE_PATH!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            if exist "%%~fI" goto :check_qt_install
        )
    )
    echo [*] Downloading and installing Visual Studio Build Tools...
    powershell -NoProfile -Command ^
        "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $out = Join-Path $env:TEMP 'vs_buildtools.exe'; (New-Object System.Net.WebClient).DownloadFile('https://aka.ms/vs/17/release/vs_buildtools.exe', $out); Start-Process $out -ArgumentList '--quiet','--wait','--norestart','--nocache','--add','Microsoft.VisualStudio.Workload.VCTools' -Wait -NoNewWindow; exit 0 } catch { Write-Host '[FATAL] Failed to install VS Build Tools'; exit 1 }" || exit /b 1
)

:check_qt_install
call :find_qt_prefix >nul 2>&1
if errorlevel 1 (
    echo [*] Installing Qt6 headless via aqtinstall...
    python --version >nul 2>&1 || (
        echo [FATAL] Python is not in PATH, so Qt cannot be installed automatically.
        exit /b 1
    )
    python -m pip install --upgrade pip aqtinstall || exit /b 1
    python -m aqt install-qt windows desktop 6.6.3 win64_msvc2019_64 --outputdir C:\Qt || exit /b 1
    call :find_qt_prefix >nul 2>&1 || (
        echo [FATAL] Qt6Config.cmake still was not found after Qt installation.
        exit /b 1
    )
)

call :find_vulkan_sdk >nul 2>&1
if not errorlevel 1 exit /b 0

echo [*] Downloading and installing Vulkan SDK silently...
powershell -NoProfile -Command ^
    "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $out = Join-Path $env:TEMP 'vulkan-installer.exe'; (New-Object System.Net.WebClient).DownloadFile('https://sdk.lunarg.com/sdk/download/latest/windows/vulkan-sdk-latest-windows.exe', $out); Start-Process -FilePath $out -ArgumentList '/S' -Wait -NoNewWindow; exit 0 } catch { Write-Host '[FATAL] Failed to install Vulkan SDK'; exit 1 }" || exit /b 1

set "VULKAN_WAIT_TRIES=0"
:wait_for_vulkan
call :find_vulkan_sdk >nul 2>&1
if not errorlevel 1 exit /b 0
set /a VULKAN_WAIT_TRIES+=1
if !VULKAN_WAIT_TRIES! geq 18 (
    echo [FATAL] Vulkan SDK installation did not finish successfully.
    exit /b 1
)
timeout /t 5 >nul
goto :wait_for_vulkan

:find_qt_prefix
set "CMAKE_PREFIX_PATH="
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$qt = Get-ChildItem -Path 'C:\Qt' -Recurse -Filter 'Qt6Config.cmake' -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -First 1; if ($qt) { [Console]::WriteLine($qt.Directory.Parent.FullName.Replace('\','/')) }"`) do (
    set "CMAKE_PREFIX_PATH=%%I"
)
if not defined CMAKE_PREFIX_PATH exit /b 1
exit /b 0

:find_vulkan_sdk
if defined VULKAN_SDK if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    set "PATH=%VULKAN_SDK%\Bin;%PATH%"
    exit /b 0
)
for /f "tokens=2,*" %%A in ('reg query "HKLM\System\CurrentControlSet\Control\Session Manager\Environment" /v VULKAN_SDK 2^>nul ^| find /I "VULKAN_SDK"') do set "VULKAN_SDK=%%B"
if defined VULKAN_SDK if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    set "PATH=%VULKAN_SDK%\Bin;%PATH%"
    exit /b 0
)
for /f "tokens=2,*" %%A in ('reg query "HKCU\Environment" /v VULKAN_SDK 2^>nul ^| find /I "VULKAN_SDK"') do set "VULKAN_SDK=%%B"
if defined VULKAN_SDK if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    set "PATH=%VULKAN_SDK%\Bin;%PATH%"
    exit /b 0
)
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$sdk = Get-ChildItem 'C:\VulkanSDK' -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1; if ($sdk -and (Test-Path (Join-Path $sdk.FullName 'Bin\glslc.exe'))) { [Console]::WriteLine($sdk.FullName) }"`) do set "VULKAN_SDK=%%I"
if defined VULKAN_SDK if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    set "PATH=%VULKAN_SDK%\Bin;%PATH%"
    exit /b 0
)
exit /b 1

:compute_sha256
set "%~2="
set "KH_HASH_TARGET=%~f1"
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath $env:KH_HASH_TARGET).Hash.ToLowerInvariant()"`) do set "%~2=%%I"
if not defined %~2 exit /b 1
exit /b 0

:find_lifter_exe
set "LIFTER_EXE="
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$exe = Get-ChildItem 'build_lifter' -Recurse -Filter 'lifter_engine.exe' -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -First 1; if ($exe) { [Console]::WriteLine($exe.FullName) }"`) do set "LIFTER_EXE=%%I"
if not defined LIFTER_EXE exit /b 1
exit /b 0

:find_runtime_exe
set "RUNTIME_EXE="
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$exe = Get-ChildItem 'build' -Recurse -Filter 'recoded.exe' -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -First 1; if ($exe) { [Console]::WriteLine($exe.FullName) }"`) do set "RUNTIME_EXE=%%I"
if not defined RUNTIME_EXE exit /b 1
exit /b 0

:find_qt_runtime_paths
set "QT_BIN_DIR="
set "QT_WINDEPLOYQT="
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$dll = Get-ChildItem 'C:\Qt' -Recurse -Filter 'Qt6Core.dll' -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -First 1; if ($dll) { [Console]::WriteLine($dll.Directory.FullName) }"`) do set "QT_BIN_DIR=%%I"
if defined QT_BIN_DIR if exist "%QT_BIN_DIR%\windeployqt.exe" set "QT_WINDEPLOYQT=%QT_BIN_DIR%\windeployqt.exe"
if not defined QT_WINDEPLOYQT (
    for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$tool = Get-ChildItem 'C:\Qt' -Recurse -Filter 'windeployqt.exe' -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -First 1; if ($tool) { [Console]::WriteLine($tool.FullName) }"`) do set "QT_WINDEPLOYQT=%%I"
)
if not defined QT_BIN_DIR exit /b 1
exit /b 0

:cmake_configure
set "SOURCE_DIR=%~1"
set "BUILD_DIR=%~2"
set "BUILD_TYPE=Release"
if "%DEBUG_MODE%"=="1" set "BUILD_TYPE=Debug"
cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%" || exit /b 1
exit /b 0

:build_lifter
call :find_lifter_exe >nul 2>&1

if "%SKIP_LIFTER_BUILD%"=="1" (
    if not defined LIFTER_EXE (
        echo [ERROR] --skip-lifter-build was requested but lifter_engine.exe is missing.
        exit /b 1
    )
    powershell -NoProfile -Command "$exe = Get-Item $env:LIFTER_EXE; $changed = Get-ChildItem 'lifter' -Recurse -File | Where-Object { $_.LastWriteTimeUtc -gt $exe.LastWriteTimeUtc } | Select-Object -First 1; if ($changed) { exit 1 } else { exit 0 }" || (
        echo [ERROR] Existing lifter build is stale; rerun without --skip-lifter-build.
        exit /b 1
    )
    echo [*] Building Lifter Engine... (skipped by --skip-lifter-build)
    exit /b 0
)

set "NEED_LIFTER_BUILD=1"
if defined LIFTER_EXE if "%FORCE_LIFT%"=="0" (
    powershell -NoProfile -Command "$exe = Get-Item $env:LIFTER_EXE; $changed = Get-ChildItem 'lifter' -Recurse -File | Where-Object { $_.LastWriteTimeUtc -gt $exe.LastWriteTimeUtc } | Select-Object -First 1; if ($changed) { exit 1 } else { exit 0 }" >nul 2>&1
    if not errorlevel 1 set "NEED_LIFTER_BUILD=0"
)

if "%NEED_LIFTER_BUILD%"=="0" (
    echo [*] Building Lifter Engine... ^(cached, skipping^)
    exit /b 0
)

echo [*] Building Lifter Engine...
if not exist build_lifter mkdir build_lifter
if exist build_lifter\CMakeCache.txt del /f /q build_lifter\CMakeCache.txt >nul 2>&1
if exist build_lifter\CMakeFiles rmdir /s /q build_lifter\CMakeFiles >nul 2>&1

call :cmake_configure "lifter" "build_lifter" || exit /b 1
cmake --build build_lifter --target lifter_engine --parallel %BUILD_JOBS% --config Release || exit /b 1
call :find_lifter_exe || (
    echo [ERROR] Built lifter_engine.exe could not be located.
    exit /b 1
)
exit /b 0

:extract_rom
if not exist recoded mkdir recoded
set "EXTRACT_STAMP=recoded\.extract_stamp"
set "EXTRACT_CACHE_KEY=rom_sha=!ACTUAL_SHA256!"

if "%FORCE_EXTRACT%"=="0" if exist "%EXTRACT_STAMP%" if exist "recoded\arm9.bin" if exist "recoded\y9.bin" if exist "recoded\overlay" (
    findstr /X /C:"!EXTRACT_CACHE_KEY!" "%EXTRACT_STAMP%" >nul 2>&1
    if not errorlevel 1 (
        echo [*] Extracting ROM... ^(cached, skipping^)
        exit /b 0
    )
)

echo [*] Extracting ROM...
set "NDSTOOL=tools\nds_extractor\ndstool.exe"
if not exist "%NDSTOOL%" (
    where ndstool.exe >nul 2>&1 || (
        echo [ERROR] ndstool.exe was not found in tools\nds_extractor or PATH.
        exit /b 1
    )
    set "NDSTOOL=ndstool.exe"
)

"%NDSTOOL%" -x "%ROM_PATH%" -9 recoded\arm9.bin -7 recoded\arm7.bin -y9 recoded\y9.bin -y7 recoded\y7.bin -d recoded\data -y recoded\overlay -t recoded\banner.bin -h recoded\header.bin || exit /b 1
> "%EXTRACT_STAMP%" echo !EXTRACT_CACHE_KEY!
exit /b 0

:run_lifter
set "GEN_DIR=runtime\src\generated"
call :compute_sha256 "%LIFTER_EXE%" LIFTER_SHA256 || exit /b 1

set "LIFT_STAMP=%GEN_DIR%\.lift_stamp"
set "LIFT_CACHE_KEY=rom_sha=!ACTUAL_SHA256! lifter_sha=!LIFTER_SHA256!"

if "%FORCE_LIFT%"=="0" if exist "%LIFT_STAMP%" if exist "%GEN_DIR%\master_registration.cpp" (
    findstr /X /C:"!LIFT_CACHE_KEY!" "%LIFT_STAMP%" >nul 2>&1
    if not errorlevel 1 (
        echo [*] Running Binary Lifter... ^(cached, skipping^)
        exit /b 0
    )
)

echo [*] Running Binary Lifter...
if exist "%GEN_DIR%" rmdir /s /q "%GEN_DIR%"
mkdir "%GEN_DIR%" || exit /b 1

echo [*] Lifting arm9.bin...
"%LIFTER_EXE%" recoded\arm9.bin "%GEN_DIR%" 0x02000000 0x02000800 -1 || exit /b 1

echo [*] Parsing y9.bin and lifting overlays...
for /f "usebackq tokens=1,2" %%A in (`powershell -NoProfile -Command "$data = [System.IO.File]::ReadAllBytes('recoded\y9.bin'); for($i = 0; $i -lt ($data.Length / 32); $i++){ $offset = $i * 32; $id = [BitConverter]::ToUInt32($data, $offset); $ram = [BitConverter]::ToUInt32($data, $offset + 4); [Console]::WriteLine(('{0} {1}' -f $id, $ram.ToString('x8'))) }"`) do (
    set "OVL_ID=%%A"
    set "RAM_ADDR=%%B"
    set "PADDED_ID=0000!OVL_ID!"
    set "PADDED_ID=!PADDED_ID:~-4!"
    set "OVL_FILE=recoded\overlay\overlay_!PADDED_ID!.bin"
    if exist "!OVL_FILE!" (
        echo [*] Lifting overlay !OVL_ID! at 0x!RAM_ADDR!...
        "%LIFTER_EXE%" "!OVL_FILE!" "%GEN_DIR%" "0x!RAM_ADDR!" "0x!RAM_ADDR!" "!OVL_ID!" || exit /b 1
    )
)

echo [*] Generating master registration...
powershell -NoProfile -Command ^
    "$genDir = 'runtime\src\generated';" ^
    "$lines = New-Object System.Collections.Generic.List[string];" ^
    "$lines.Add('#include ' + [char]34 + 'memory_map.h' + [char]34);" ^
    "$lines.Add('void arm9_register(NDSMemory* mem);');" ^
    "Get-ChildItem $genDir -Filter 'overlay_*_reg.cpp' | Sort-Object Name | ForEach-Object { if ($_.BaseName -match '^overlay_(\d+)_reg$') { $lines.Add(('void overlay_{0}_register(NDSMemory* mem);' -f $Matches[1])) } };" ^
    "$lines.Add('void RegisterAllLiftedFunctions(NDSMemory* mem) {');" ^
    "$lines.Add('    arm9_register(mem);');" ^
    "Get-ChildItem $genDir -Filter 'overlay_*_reg.cpp' | Sort-Object Name | ForEach-Object { if ($_.BaseName -match '^overlay_(\d+)_reg$') { $lines.Add(('    overlay_{0}_register(mem);' -f $Matches[1])) } };" ^
    "$lines.Add('}');" ^
    "[System.IO.File]::WriteAllLines((Join-Path $genDir 'master_registration.cpp'), $lines)" || exit /b 1

> "%LIFT_STAMP%" echo !LIFT_CACHE_KEY!
exit /b 0

:build_runtime
echo [*] Configuring runtime build...
if not exist build mkdir build
if exist build\CMakeCache.txt del /f /q build\CMakeCache.txt >nul 2>&1
if exist build\CMakeFiles rmdir /s /q build\CMakeFiles >nul 2>&1

call :cmake_configure "." "build" || exit /b 1

set "BUILD_CONFIG=Release"
if "%DEBUG_MODE%"=="1" set "BUILD_CONFIG=Debug"
echo [*] Building runtime_engine...
cmake --build build --target runtime_engine --parallel %BUILD_JOBS% --config %BUILD_CONFIG% || exit /b 1

call :find_runtime_exe || (
    echo [ERROR] Built recoded.exe could not be located.
    exit /b 1
)
call :deploy_qt_runtime || exit /b 1
exit /b 0

:deploy_qt_runtime
call :find_qt_runtime_paths || (
    echo [ERROR] Qt runtime files could not be located under C:\Qt.
    exit /b 1
)
for %%I in ("%RUNTIME_EXE%") do set "RUNTIME_DIR=%%~dpI"

echo [*] Deploying Qt runtime next to recoded.exe...
if defined QT_WINDEPLOYQT (
    set "DEPLOY_MODE=--release"
    if "%DEBUG_MODE%"=="1" set "DEPLOY_MODE=--debug"
    "%QT_WINDEPLOYQT%" %DEPLOY_MODE% --no-compiler-runtime "%RUNTIME_EXE%" || exit /b 1
    exit /b 0
)

copy /y "%QT_BIN_DIR%\Qt6Core.dll" "%RUNTIME_DIR%" >nul || exit /b 1
copy /y "%QT_BIN_DIR%\Qt6Gui.dll" "%RUNTIME_DIR%" >nul || exit /b 1
copy /y "%QT_BIN_DIR%\Qt6Widgets.dll" "%RUNTIME_DIR%" >nul || exit /b 1
if not exist "%RUNTIME_DIR%platforms" mkdir "%RUNTIME_DIR%platforms"
copy /y "%QT_BIN_DIR%\..\plugins\platforms\qwindows.dll" "%RUNTIME_DIR%platforms\" >nul || exit /b 1
exit /b 0

:main
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%" >nul

set "DEBUG_MODE=0"
set "SKIP_DEPS=0"
set "SKIP_LIFTER_BUILD=0"
set "FORCE_EXTRACT=0"
set "FORCE_LIFT=0"
set "BUILD_JOBS="
set "LIFT_JOBS="
set "ROM_PATH="
set "RUNTIME_EXE="
set "LIFTER_EXE="

call :guard_against_msys2_path || goto :fail
call :parse_args %* || goto :fail

if not defined ROM_PATH (
    call :usage
    goto :fail
)

if not exist "%ROM_PATH%" if exist "%ROM_PATH%.partaa" (
    echo [*] Recombining split ROM...
    copy /b "%ROM_PATH%.part*" "%ROM_PATH%" >nul || goto :fail
)
if not exist "%ROM_PATH%" (
    echo [ERROR] ROM file not found: %ROM_PATH%
    goto :fail
)

if "%SKIP_DEPS%"=="0" (
    call :install_dependencies || goto :fail
)

if not defined BUILD_JOBS (
    if defined NUMBER_OF_PROCESSORS (
        set "BUILD_JOBS=%NUMBER_OF_PROCESSORS%"
    ) else (
        set "BUILD_JOBS=1"
    )
)
if not defined LIFT_JOBS (
    set "LIFT_JOBS=%BUILD_JOBS%"
    powershell -NoProfile -Command "$jobs=[int]$env:LIFT_JOBS; if($jobs -gt 4){$jobs=4}; [Console]::WriteLine($jobs)" > "%TEMP%\kh_lift_jobs.txt" || goto :fail
    set /p LIFT_JOBS=<"%TEMP%\kh_lift_jobs.txt"
    del "%TEMP%\kh_lift_jobs.txt" >nul 2>&1
)

call :find_qt_prefix || (
    echo [FATAL] Qt6Config.cmake was not found. Install Qt 6.6.3 MSVC 64-bit or rerun without --skip-deps.
    goto :fail
)
call :find_vulkan_sdk || (
    echo [FATAL] Vulkan SDK was not found. Install LunarG Vulkan SDK or rerun without --skip-deps.
    goto :fail
)

echo [*] Using %BUILD_JOBS% compile jobs and %LIFT_JOBS% lift jobs.

set "KNOWN_SHA256=929f36f1e09b6b0962ac718332033bbd519f2edede18a3ab65f425ddc66e3fd3"
call :compute_sha256 "%ROM_PATH%" ACTUAL_SHA256 || goto :fail
echo [*] Validating ROM checksum...
echo [*] Calculated SHA-256: !ACTUAL_SHA256!
if /I not "!ACTUAL_SHA256!"=="!KNOWN_SHA256!" (
    echo [WARNING] Checksum does not match the known good US dump.
    echo [WARNING] Proceeding anyway, but compilation or runtime errors may occur.
)

call :build_lifter || goto :fail
call :extract_rom || goto :fail
call :run_lifter || goto :fail
call :build_runtime || goto :fail

echo.
echo ============================================================
echo [SUCCESS] Build completed successfully.
echo [*] Executable: !RUNTIME_EXE!
echo ============================================================

popd >nul
exit /b 0

:fail
set "ERR=%ERRORLEVEL%"
if not defined ERR set "ERR=1"
popd >nul 2>nul
exit /b %ERR%
