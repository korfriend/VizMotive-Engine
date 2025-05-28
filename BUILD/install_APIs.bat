@echo off
cd /d %~dp0
setlocal enabledelayedexpansion
REM ------------------------------------------
REM Header files in HighAPIs folder (outdated copy)
REM Target: ..\Install\vzm2\
set "highapis=VzComponentAPIs.h VzEngineAPIs.h VzActor.h VzLight.h VzArchive.h VzCamera.h VzGeometry.h VzMaterial.h VzRenderer.h VzScene.h VzTexture.h"
for %%F in (%highapis%) do (
    xcopy "..\EngineCore\HighAPIs\%%F" "..\Install\vzm2\" /D /Y
)
REM ------------------------------------------
REM Individual header files in Libs folder (outdated copy)
REM Target: ..\Install\vzm2\utils\
xcopy "..\EngineCore\CommonInclude.h" "..\Install\vzm2\utils\" /D /Y

REM ------------------------------------------
REM Copy entire DirectXMath folder (including subfolders)
REM robocopy performs incremental copy by default, copying only files that are newer than the target
robocopy "..\EngineCore\Utils\DirectXMath" "..\Install\vzm2\utils\DirectXMath" /E /XO
REM ------------------------------------------
REM Specified header files in Utils folder (outdated copy)
REM Target: ..\Install\vzm2\utils\
set "utils=vzMath.h Geometrics.h GeometryGenerator.h Backlog.h EventHandler.h Helpers.h JobSystem.h Profiler.h Timer.h Platform.h Config.h Random.h"
for %%F in (%utils%) do (
    xcopy "..\EngineCore\Utils\%%F" "..\Install\vzm2\utils\" /D /Y
)


REM ------------------------------------------------
REM Check if a configuration argument is provided
if "%~1"=="" (
    echo Usage: %~nx0 Debug^|Release
    goto :EOF
)

set "config=%~1"
REM ------------------------------------------------
REM Set variables according to configuration argument (case-insensitive)

if /I "%config%"=="Debug" (
    set "folder=x64_Debug"
    set "libFile=VizEngined.lib"
    set "dllDest=..\Install\bin\debug_dll"
) else if /I "%config%"=="Release" (
    set "folder=x64_Release"
    set "libFile=VizEngine.lib"
    set "dllDest=..\Install\bin\release_dll"
) else (
    echo Invalid configuration specified. Use Debug or Release.
    goto :EOF
)
    
REM Check if source folder exists
if exist "..\bin\!folder!\" (
    
    REM ----------------------------
    REM [1] Copy Lib file (..\Install\lib\)
    if exist "..\bin\!folder!\!libFile!" (
        echo Copying ..\bin\!folder!\!libFile! to ..\Install\lib\
        xcopy "..\bin\!folder!\!libFile!" "..\Install\lib\" /D /Y /I
    ) else (
        echo Lib file does not exist: ..\bin\!folder!\!libFile!
    )
        
    REM ----------------------------
    REM [2] Copy DLL files and dxc.exe (excluding DLLs with PluginSample in name)
    REM Create target folder (if it doesn't exist)
    if not exist "!dllDest!\" (
        mkdir "!dllDest!"
    )
        
    echo Copying DLLs from ..\bin\!folder! to !dllDest! ^(excluding PluginSample files^)
    for %%F in ("..\bin\!folder!\*.dll") do (
        REM Check if filename contains "PluginSample"
        echo %%~nxF | findstr /I "PluginSample" >nul
        if errorlevel 1 (
            echo   Copying %%~nxF
            xcopy "%%F" "!dllDest!\" /D /Y /I
        ) else (
            echo   Skipping %%~nxF ^(contains PluginSample^)
        )
    )
        
    set "sourceDir=..\dxc\windows"
    set "targetParentDir=..\bin"
    if not exist "!sourceDir!" (
        echo Source directory does not exist: !sourceDir!
        goto :eof
    )
    if not exist "!targetParentDir!" (
        echo Target parent directory does not exist: !targetParentDir!
        goto :eof
    )
    for /d %%d in ("..\bin\*") do (
        REM Use delayed expansion to get the directory name
        set "targetDir=%%~fd"
        call :CopyFilesToTarget "!targetDir!"
    )


    REM ----------------------------
    REM [3] Copy dxc.exe
    if exist "..\bin\!folder!\dxc.exe" (
        echo Copying dxc.exe from ..\bin\!folder! to !dllDest!
        xcopy "..\bin\!folder!\dxc.exe" "!dllDest!\" /D /Y /I
    ) else (
        echo dxc.exe does not exist: ..\bin\!folder!\dxc.exe
    )
        
) else (
    echo Source folder does not exist: ..\bin\!folder!\
)

goto :eof

:CopyFilesToTarget
setlocal
set "targetDir=%~1"
echo Copying files to !targetDir!

for %%f in ("%sourceDir%\*.*") do (
    set "filename=%%~nxf"
    REM Skip certain files
    echo !filename! | findstr /I "DumpStack.log" >nul
    if errorlevel 1 (
        echo   Copying !filename! to !targetDir!
        xcopy "%%f" "!targetDir!\" /D /Y /I >nul
    ) else (
        echo   Skipping !filename! (restricted)
    )
)
endlocal