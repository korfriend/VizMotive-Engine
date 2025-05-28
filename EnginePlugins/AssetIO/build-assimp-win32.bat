@echo off
rem -----------------------------------------------
rem build-assimp.bat  | VizMotive simple build/install script
rem -----------------------------------------------
setlocal EnableDelayedExpansion

:: 1) User configurable variables
set CONFIGS=Debug Release
set BUILD_DIR=%~dp0External\assimp\build
set INSTALL_DIR=%~dp0\ThirdParty\assimp

:: 2) Setup Visual Studio compiler environment
::    (For VS 2022; change if using a different version)
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo === [Configure] ===========================================================
cmake -S "%~dp0External\assimp" ^
      -B "%BUILD_DIR%" ^
      -G "Visual Studio 17 2022" -A x64 ^
      -DBUILD_SHARED_LIBS=OFF ^
      -DUSE_STATIC_CRT=OFF ^
      -DCMAKE_DEBUG_POSTFIX=d ^
      -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
      -DLIBRARY_SUFFIX="-vc143-md" ^
      -DASSIMP_BUILD_ASSIMP_TOOLS=OFF ^
      -DASSIMP_LIBRARY_SUFFIX="-vc143-md" ^
      -DASSIMP_BUILD_TESTS=OFF ^
      -DASSIMP_INSTALL=ON ^
      -DASSIMP_INJECT_DEBUG_POSTFIX=ON ^
      -DASSIMP_BUILD_DRACO=ON

if errorlevel 1 goto :err

rem 3) Loop over configurations
for %%C in (%CONFIGS%) do (
    echo === [Build %%C] ======================================================
    cmake --build "%BUILD_DIR%" --config %%C --target install
    if errorlevel 1 goto :err
)

rem 4) Delete unwanted libs
if exist "%INSTALL_DIR%\lib\draco_md.lib" del "%INSTALL_DIR%\lib\draco_md.lib"
if exist "%INSTALL_DIR%\lib\draco_mdd.lib" del "%INSTALL_DIR%\lib\draco_mdd.lib"
if exist "%INSTALL_DIR%\lib\zlibstatic_md.lib" del "%INSTALL_DIR%\lib\zlibstatic_md.lib"
if exist "%INSTALL_DIR%\lib\zlibstatic_mdd.lib" del "%INSTALL_DIR%\lib\zlibstatic_mdd.lib"

rem 5) Rename libs
if exist "%INSTALL_DIR%\lib\draco.lib" rename "%INSTALL_DIR%\lib\draco.lib" "draco_md.lib"
if exist "%INSTALL_DIR%\lib\dracod.lib" rename "%INSTALL_DIR%\lib\dracod.lib" "draco_mdd.lib"
if exist "%INSTALL_DIR%\lib\zlibstatic.lib" rename "%INSTALL_DIR%\lib\zlibstatic.lib" "zlibstatic_md.lib"
if exist "%INSTALL_DIR%\lib\zlibstaticd.lib" rename "%INSTALL_DIR%\lib\zlibstaticd.lib" "zlibstatic_mdd.lib"

echo *** BUILD and INSTALL SUCCESS ***
exit /b 0

:err
echo *** BUILD FAILED ***
exit /b 1
