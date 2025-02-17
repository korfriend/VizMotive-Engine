@echo off
setlocal enabledelayedexpansion
chcp 65001
:: -----------------------------------------------------------
:: 0. Check if Git username is "korfriend"
:: -----------------------------------------------------------
for /f "delims=" %%G in ('git config user.name') do set "GITUSER=%%G"
echo Current Git user: %GITUSER%
if /I "%GITUSER%"=="korfriend" (
    echo Git user verification complete: Welcome, dojo
    call %~dp0update_version_check.bat
) else (
    echo Install and Version Update script can only be executed by github account 'korfriend'
)
REM include all dependency PROJECTS 
set "PROJECTS=%~dp0Engine_Windows.vcxproj"
set "PROJECTS=%PROJECTS% %~dp0..\GraphicsBackends\GBackendDX12.vcxproj"
set "PROJECTS=%PROJECTS% %~dp0..\GraphicsBackends\GBackendVulkan.vcxproj"
set "PROJECTS=%PROJECTS% %~dp0..\EngineShaders\ShaderEngine\ShaderEngine.vcxproj"
set "PROJECTS=%PROJECTS% %~dp0..\EnginePlugins\AssetIO\AssetIO.vcxproj"
for %%C in (Debug Release) do (
    set "CONFIG=%%C"
    for %%P in (%PROJECTS%) do (
        echo Building project %%~nxP with configuration !CONFIG!
        msbuild "%%~P" /p:Configuration=!CONFIG! /p:Platform=x64 /clp:ErrorsOnly
        set "BUILDRESULT=!ERRORLEVEL!"
        if !BUILDRESULT! neq 0 (
            echo Build of project %%~nxP [!CONFIG!] failed with error code !BUILDRESULT!.
            goto finish
        ) else (
            echo Build of project %%~nxP [!CONFIG!] succeeded.
        )
    )
)
:finish
::call revert_update_version.bat !BUILDRESULT!
if /I "%GITUSER%"=="korfriend" (
    echo Build: !BUILDRESULT!
    echo Git user verification complete: Welcome, dojo
    call %~dp0revert_update_version.bat !BUILDRESULT!
)
if /I !BUILDRESULT! == 0 (
    call %~dp0install_APIs.bat Debug
    call %~dp0install_APIs.bat Release
    call %~dp0install_EngineAPIs.bat
)
endlocal