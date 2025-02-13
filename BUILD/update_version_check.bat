@echo off
setlocal enabledelayedexpansion
REM ============================================================
REM 0. Basic Variable Settings
REM ------------------------------------------------------------
REM %~dp0 : Current batch file directory (including final '\')
set "ENGINE_COMP=%~dp0..\EngineCore\Components\Components.h"
set "ENGINE_GCOMP=%~dp0..\EngineCore\Components\GComponents.h"
set "INSTALL_COMP=%~dp0..\Install\vzmcore\Components.h"
set "INSTALL_GCOMP=%~dp0..\Install\vzmcore\GComponents.h"
REM ============================================================
REM 1. Compare Header Files (Components.h and GComponents.h)
REM ------------------------------------------------------------
echo =====================================================
echo [Step 1] Starting header file comparison...
echo Comparing: 
echo   ENGINE Components: %ENGINE_COMP%
echo   INSTALL Components: %INSTALL_COMP%
echo   ENGINE GComponents: %ENGINE_GCOMP%
echo   INSTALL GComponents: %INSTALL_GCOMP%
echo ----------------------------------------------------
set "updateMode=none"
REM Compare Components.h (binary comparison)
fc /B "%ENGINE_COMP%" "%INSTALL_COMP%" >nul
if errorlevel 1 (
    echo [INFO] Components.h contents differ.
    set "updateMode=full"
) else (
    REM Compare GComponents.h (binary comparison)
    fc /B "%ENGINE_GCOMP%" "%INSTALL_GCOMP%" >nul
    if errorlevel 1 (
        echo [INFO] GComponents.h contents differ.
        set "updateMode=full"
    ) else (
        echo [INFO] Both header files are identical.
    )
)
REM ============================================================
REM 2. Check for EngineCore modifications
REM    (Only check if header files are identical)
REM    Uses modification date of %~dp0..\EngineCore\Common\Version.cpp as reference
REM ------------------------------------------------------------
if /I "%updateMode%"=="none" (
    echo ----------------------------------------------------
    echo [Step 2] Checking EngineCore modifications: Comparing latest modification dates of code and DLLs
    REM BIN folder: Location of latest DLLs (e.g., Release configuration)
    set "BIN_DIR=%~dp0..\bin"
    REM ENGINE_DIR: Complete EngineCore folder including subdirectories
    set "ENGINE_DIR=%~dp0..\EngineCore"
    REM Use PowerShell to compare the most recent modification times (in Ticks)
    REM between all existing files in ENGINE_DIR and BIN_DIR
    for /f "usebackq delims=" %%R in (`powershell -NoProfile -Command "if ((Get-ChildItem -Path '!ENGINE_DIR!' -Recurse -File | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime.ToUniversalTime().Ticks -gt (Get-ChildItem -Path '!BIN_DIR!' -Recurse -Filter *.dll | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime.ToUniversalTime().Ticks) { Write-Output update } else { Write-Output no }"`) do set "COMPARE_RESULT=%%R"
    echo Comparison result: !COMPARE_RESULT!
    if /I "!COMPARE_RESULT!"=="update" (
        echo [INFO] EngineCore code's latest modification is more recent than DLL's.
        echo Setting update mode to VERSION_CPP only based on update requirement.
        set "updateMode=v"
    ) else (
        echo [INFO] DLLs are up to date, no update needed.
    )
)
REM ============================================================
REM 3. Call update_version.bat if needed
REM ------------------------------------------------------------
if /I "%updateMode%"=="full" (
    echo ----------------------------------------------------
    echo [Step 3] Executing update_version.bat, FULL update...
    call "%~dp0update_version.bat"
) else if /I "%updateMode%"=="v" (
    echo ----------------------------------------------------
    echo [Step 3] Executing update_version.bat, VERSION_CPP update only...
    call "%~dp0update_version.bat" v
) else (
    echo ----------------------------------------------------
    echo [Step 3] No file changes detected, skipping update_version.bat
)
endlocal
exit /b 0