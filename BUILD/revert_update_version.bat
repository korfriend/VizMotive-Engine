@echo off
setlocal enabledelayedexpansion
:: Receive build success status as argument
set "BUILD_SUCCESS=%1"
echo Post-build script started
echo Build Result: %BUILD_SUCCESS%
set "COMP_FILE=%~dp0..\EngineCore\Components\Components.h"
set "VERSION_CPP=%~dp0..\EngineCore\Common\Version.cpp"
:: Check build success status
if "%BUILD_SUCCESS%" == "0" (
    echo Build successful.
    goto :end
) else (
    echo Build failed.
    echo Restoring original files from backups...
    copy /Y "%COMP_FILE%.bak" "%COMP_FILE%"
    copy /Y "%VERSION_CPP%.bak" "%VERSION_CPP%"
    echo [INFO] Restoration completed.
    goto :end
)
:end
for %%F in ("%COMP_FILE%.bak" "%VERSION_CPP%.bak") do (
    if exist "%%F" (
        del /Q "%%F"
        echo Backup file "%%F" deleted.
    )
)
endlocal