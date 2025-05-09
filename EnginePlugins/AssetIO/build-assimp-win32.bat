@echo off
rem ───────────────────────────────────────────────
rem build-assimp.bat  |  VizMotive 전용 간이 빌드/설치 스크립트
rem ───────────────────────────────────────────────
setlocal EnableDelayedExpansion

:: 1) 사용자가 바꿔 쓸 수 있는 변수
set CONFIGS=Debug Release
set BUILD_DIR=%~dp0External\assimp\build
set INSTALL_DIR=%~dp0\ThirdParty\assimp

:: 2) Visual Studio 컴파일러(vcvars) 환경 설정
::    (VS 2022 기준, 다른 버전이면 경로 수정)
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

rem 2) Loop over configs
for %%C in (%CONFIGS%) do (
    echo === [Build %%C] ======================================================
    cmake --build "%BUILD_DIR%" --config %%C --target install
    if errorlevel 1 goto :err
)

del "%INSTALL_DIR%\lib\draco_md.lib"
del "%INSTALL_DIR%\lib\draco_mdd.lib"
del "%INSTALL_DIR%\lib\zlibstatic_md.lib"
del "%INSTALL_DIR%\lib\zlibstatic_mdd.lib"

@echo off
if exist "%INSTALL_DIR%\lib\draco.lib" (
    rename "%INSTALL_DIR%\lib\draco.lib" "draco_md.lib"
) 
if exist "%INSTALL_DIR%\lib\dracod.lib" (
    rename "%INSTALL_DIR%\lib\dracod.lib" "draco_mdd.lib"
)
if exist "%INSTALL_DIR%\lib\zlibstatic.lib" (
    rename "%INSTALL_DIR%\lib\zlibstatic.lib" "zlibstatic_md.lib"
)
if exist "%INSTALL_DIR%\lib\zlibstaticd.lib" (
    rename "%INSTALL_DIR%\lib\zlibstaticd.lib" "zlibstatic_mdd.lib"
)

echo *** BUILD and INSTALL SUCCESS ***
exit /b 0

:err
echo *** BUILD FAILED ***
exit /b 1
