@echo off
cd /d %~dp0
setlocal enabledelayedexpansion
REM ------------------------------------------
REM Header files in Engine-level components and apis folder (outdated copy)
REM Target: ..\Install\vzmcore\
set "enginecomps=Components\Components.h Components\GComponents.h CommonInclude.h"
for %%F in (%enginecomps%) do (
    xcopy "..\EngineCore\%%F" "..\Install\vzmcore\" /D /Y
)

set "backendinterfaces=GBackend.h GBackendDevice.h"
for %%F in (%backendinterfaces%) do (
    xcopy "..\EngineCore\GBackend\%%F" "..\Install\vzmcore\GBackend\" /D /Y
)

set "enginecomps=Components.h GComponents.h"
for %%F in (%enginecomps%) do (
    xcopy "..\EngineCore\Components\%%F" "..\Install\vzmcore\" /D /Y
)

REM ------------------------------------------
REM Copy entire DirectXMath folder (including subfolders)
REM robocopy performs incremental copy by default, copying only files that are newer than the target
robocopy "..\EngineCore\Utils\DirectXMath" "..\Install\vzmcore\utils\DirectXMath" /E /XO
REM ------------------------------------------
REM Specified header files in Utils folder (outdated copy)
REM Target: ..\Install\vzmcore\utils\
set "utils=vzMath.h Geometrics.h GeometryGenerator.h Backlog.h EventHandler.h Helpers.h JobSystem.h Profiler.h Timer.h Platform.h Config.h"
for %%F in (%utils%) do (
    xcopy "..\EngineCore\Utils\%%F" "..\Install\vzmcore\utils\" /D /Y
)

endlocal