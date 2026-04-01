@echo off
setlocal EnableDelayedExpansion

:: ============================================================
:: build_windows.bat  --  Build HostGUI.exe on Windows
::
:: Prerequisites (install once):
::   1. Qt 6.x for Windows (MinGW 64-bit recommended)
::      https://www.qt.io/download-qt-installer
::      Typical install path: C:\Qt\6.x.x\mingw_64
::   2. CMake 3.16+  (included in Qt install, or https://cmake.org)
::   3. Python 3     (https://python.org) -- needed at runtime for
::                   the TCP relay, not at build time
::
:: Usage:
::   build_windows.bat                    -- auto-detect Qt
::   build_windows.bat C:\Qt\6.7.2\mingw_64   -- explicit Qt root
:: ============================================================

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build_win"
set "DIST_DIR=%SCRIPT_DIR%dist\HostGUI_Windows"

:: ---- Resolve Qt root ----------------------------------------
if not "%~1"=="" (
    set "QT_ROOT=%~1"
    echo [build] Using Qt from argument: !QT_ROOT!
    goto :found_qt
)

:: Auto-search common install locations
for %%D in (C D E F) do (
    for /D %%V in (%%D:\Qt\6.*) do (
        for /D %%C in (%%V\mingw*_64 %%V\msvc*_64) do (
            if exist "%%C\bin\qmake.exe" (
                set "QT_ROOT=%%C"
                echo [build] Found Qt: %%C
                goto :found_qt
            )
        )
    )
    for /D %%V in (%%D:\Qt\5.*) do (
        for /D %%C in (%%V\mingw*_64 %%V\msvc*_64) do (
            if exist "%%C\bin\qmake.exe" (
                set "QT_ROOT=%%C"
                echo [build] Found Qt: %%C
                goto :found_qt
            )
        )
    )
)

echo.
echo [ERROR] Qt not found.  Please either:
echo   1. Run this script with Qt path as argument:
echo        build_windows.bat C:\Qt\6.7.2\mingw_64
echo   2. Or install Qt from https://www.qt.io/download-qt-installer
echo      (select "Qt 6.x" + "MinGW 64-bit" component)
echo.
exit /b 1

:found_qt
set "QT_BIN=%QT_ROOT%\bin"

:: ---- Detect compiler / generator ----------------------------
set "CMAKE_GENERATOR="
set "EXTRA_CMAKE_ARGS="

:: MinGW?
if exist "%QT_BIN%\qmake.exe" (
    :: Try to find mingw32-make alongside Qt
    for /D %%T in (%QT_ROOT%\..\..\..\Tools\mingw*) do (
        if exist "%%T\bin\mingw32-make.exe" (
            set "MINGW_BIN=%%T\bin"
            goto :found_mingw
        )
    )
    :: Try PATH
    where mingw32-make >nul 2>&1 && set "MINGW_BIN=" && goto :found_mingw
    where make >nul 2>&1 && set "MINGW_BIN=" && goto :use_nmake
)

:use_nmake
:: Fall back to MSVC NMake
set "CMAKE_GENERATOR=NMake Makefiles"
set "EXTRA_CMAKE_ARGS=-DCMAKE_BUILD_TYPE=Release"
echo [build] Using NMake (MSVC) generator
goto :setup_cmake

:found_mingw
set "CMAKE_GENERATOR=MinGW Makefiles"
set "EXTRA_CMAKE_ARGS=-DCMAKE_BUILD_TYPE=Release"
if defined MINGW_BIN (
    set "PATH=%MINGW_BIN%;%PATH%"
    echo [build] Added MinGW to PATH: !MINGW_BIN!
)
echo [build] Using MinGW Makefiles generator

:setup_cmake
:: ---- Find cmake ---------------------------------------------
set "CMAKE_EXE="
where cmake >nul 2>&1 && set "CMAKE_EXE=cmake" && goto :run_cmake
:: Qt bundled cmake
if exist "%QT_ROOT%\..\..\..\Tools\CMake_64\bin\cmake.exe" (
    set "CMAKE_EXE=%QT_ROOT%\..\..\..\Tools\CMake_64\bin\cmake.exe"
    set "PATH=%QT_ROOT%\..\..\..\Tools\CMake_64\bin;%PATH%"
    goto :run_cmake
)
:: SimplicityStudio bundled cmake (commonly present on this machine)
if exist "C:\SiliconLabs\SimplicityStudio\v5\developer\adapter_packs\cmake\bin\cmake.exe" (
    set "CMAKE_EXE=C:\SiliconLabs\SimplicityStudio\v5\developer\adapter_packs\cmake\bin\cmake.exe"
    goto :run_cmake
)
echo.
echo [ERROR] cmake not found. Install CMake from https://cmake.org
echo         or install the "CMake" component via the Qt installer.
exit /b 1

:run_cmake
set "PATH=%QT_BIN%;%PATH%"

echo.
echo [build] Configuring with CMake ...
echo [build]   Source: %SCRIPT_DIR%
echo [build]   Build:  %BUILD_DIR%
echo [build]   Qt:     %QT_ROOT%
echo.

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

"%CMAKE_EXE%" -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" ^
    -G "%CMAKE_GENERATOR%" ^
    -DCMAKE_PREFIX_PATH="%QT_ROOT%" ^
    %EXTRA_CMAKE_ARGS%

if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

echo.
echo [build] Compiling ...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release -- -j4

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [build] Build succeeded.

:: ---- Package with windeployqt --------------------------------
set "EXE_PATH=%BUILD_DIR%\HostGUI.exe"
if not exist "%EXE_PATH%" (
    :: CMake may put it in a Release subfolder with some generators
    if exist "%BUILD_DIR%\Release\HostGUI.exe" set "EXE_PATH=%BUILD_DIR%\Release\HostGUI.exe"
)

if not exist "%EXE_PATH%" (
    echo [WARN] HostGUI.exe not found at expected path, skipping deployment.
    goto :done
)

echo [build] Running windeployqt ...
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
copy "%EXE_PATH%" "%DIST_DIR%\HostGUI.exe" >nul

"%QT_BIN%\windeployqt.exe" --release "%DIST_DIR%\HostGUI.exe"

echo.
echo [build] ============================================
echo [build] Windows release ready: %DIST_DIR%
echo [build] ============================================
echo [build] To run on another Windows PC:
echo [build]   1. Copy the entire HostGUI_Windows folder
echo [build]   2. Install Python 3 on target (for TCP relay)
echo [build]   3. Run HostGUI.exe
echo.

:done
endlocal
