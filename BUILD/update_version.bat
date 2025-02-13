@echo off
setlocal enabledelayedexpansion

:: -----------------------------------------------------------
:: Backup
:: -----------------------------------------------------------
set "COMP_PATH=%~dp0..\EngineCore\Components\Components.h"
set "VERSION_CPP=%~dp0..\EngineCore\Common\Version.cpp"
::echo [INFO] Creating backup files...
::copy /Y "%COMP_PATH%" "%COMP_PATH%.bak"
::copy /Y "%VERSION_CPP%" "%VERSION_CPP%.bak"

REM ============================================================
REM Check arguments: If argument is "v", skip Components.h update and only update VERSION_CPP
REM ============================================================
if /I "%~1"=="v" (
    echo Argument "v" detected: Proceeding with VERSION_CPP update only.
    goto UpdateVersionCPP
)

copy /Y "%COMP_PATH%" "%COMP_PATH%.bak"
:: -----------------------------------------------------------
:: 1. Set File Paths
::    Set Components.h file location relative to batch file directory
::    (Example: Batch file location: MyScripts\build.bat, Components.h location: EngineCore\Components\Components.h)
:: -----------------------------------------------------------
:: %~dp0 represents the batch file directory (including trailing '\')
echo File to modify: %COMP_PATH%

:: -----------------------------------------------------------
:: 2. Extract version string ("VZ::YYYYMMDD_x") from Components.h
::    Use PowerShell's Select-String to find strings matching regex 'VZ::\d{8}_\d+'
:: -----------------------------------------------------------
for /f "delims=" %%A in ('powershell -NoProfile -Command "Select-String -Path '%COMP_PATH%' -Pattern 'VZ::\d{8}_\d+' | ForEach-Object { $_.Matches } | ForEach-Object { $_.Value }"') do (
    set "VERSION=%%A"
    goto :found
)
:found
if not defined VERSION (
    echo [%date% %time%] Error: Cannot find version string in %COMP_PATH%
    exit /b 1
)
echo Current version string: %VERSION%

:: -----------------------------------------------------------
:: 3. Split version string (based on underscore '_')
::    Example: "VZ::20250211_0" ¡æ datePart="VZ::20250211" , counter="0"
:: -----------------------------------------------------------
for /f "tokens=1,2 delims=_" %%a in ("%VERSION%") do (
    set "datePart=%%a"
    set "counter=%%b"
)
echo Date part: %datePart%
echo Current counter: %counter%

:: -----------------------------------------------------------
:: 4. Remove "VZ::" from datePart to extract actual date (YYYYMMDD)
:: -----------------------------------------------------------
set "dateOnly=%datePart:~4%"
echo Extracted date: %dateOnly%

:: -----------------------------------------------------------
:: 5. Get today's date in YYYYMMDD format using PowerShell
:: -----------------------------------------------------------
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd"') do set "TODAY=%%i"
echo Today date: %TODAY%

:: -----------------------------------------------------------
:: 6. Compare today's date with the file date
:: If they are equal then increment the counter by 1
:: If they are different then update datePart with today's date and reset counter to 0
:: -----------------------------------------------------------
if "%dateOnly%"=="%TODAY%" (
    set /a newCounter=%counter% + 1
) else (
    set "datePart=VZ::%TODAY%"
    set "newCounter=0"
)
echo New counter: %newCounter%

:: -----------------------------------------------------------
:: 7. Generate new version string
:: -----------------------------------------------------------
set "newVersion=%datePart%_%newCounter%"
echo New version string: %newVersion%

:: -----------------------------------------------------------
:: 8. Update version string in Components.h file (using PowerShell -replace)
:: -----------------------------------------------------------
powershell -NoProfile -Command "(Get-Content '%COMP_PATH%') -replace 'VZ::\d{8}_\d+', '%newVersion%' | Set-Content '%COMP_PATH%'"
echo File %COMP_PATH% has been updated to %newVersion%

:: -----------------------------------------------------------
:: 9. Set Version.cpp file path
::    - Set relative path to Version.cpp based on batch file location
::    - For example, if batch file and Version.cpp are in the same folder, use as below
:: -----------------------------------------------------------
:UpdateVersionCPP
echo =================== Version Update
echo File to update: %VERSION_CPP%
copy /Y "%VERSION_CPP%" "%VERSION_CPP%.bak"

:: -----------------------------------------------------------
:: 10. Extract current revision value from Version.cpp
:: Get token5 (number+semicolon) from line matching "const int revision = 3;"
:: -----------------------------------------------------------
for /f "tokens=2 delims==" %%A in ('findstr /R /C:"const int revision =" "%VERSION_CPP%"') do (
    for /f "tokens=1" %%B in ("%%A") do (
        set "currentRevision=%%B"
        goto :gotRevision
    )
)
:gotRevision
if not defined currentRevision (
    echo [ERROR] Cannot find revision value in %VERSION_CPP%
    exit /b 1
)
:: Remove semicolon e.g., "3;" -> "3"
set "currentRevision=%currentRevision:;=%"
echo Current revision: %currentRevision%

:: -----------------------------------------------------------
:: 11. Increment revision value by 1
:: -----------------------------------------------------------
set /a newRevision=%currentRevision% + 1
echo New revision: %newRevision%

:: -----------------------------------------------------------
:: 12. Update revision value in Version.cpp (using PowerShell)
::    - Replace matches of regex 'const int revision = (\d+);' with new revision value
:: -----------------------------------------------------------
powershell -NoProfile -Command ^
    "(Get-Content '%VERSION_CPP%') -replace 'const int revision = (\d+);', 'const int revision = %newRevision%;' | Set-Content '%VERSION_CPP%'"

echo %VERSION_CPP% file's revision value has been updated to %newRevision%

:: -----------------------------------------------------------
:: build+. Execute build (example: using msbuild, modify as needed)
:: -----------------------------------------------------------
::echo Starting build...
::msbuild MyProject.sln /p:Configuration=Release

endlocal