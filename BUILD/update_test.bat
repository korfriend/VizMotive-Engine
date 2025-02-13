@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM 0. 기본 변수 설정
REM ------------------------------------------------------------
REM %~dp0 : 현재 배치 파일이 있는 디렉터리 (마지막 '\' 포함)
set "ENGINE_COMP=%~dp0..\EngineCore\Components\Components.h"
set "ENGINE_GCOMP=%~dp0..\EngineCore\Components\GComponents.h"
set "INSTALL_COMP=%~dp0..\Install\vzmcore\Components.h"
set "INSTALL_GCOMP=%~dp0..\Install\vzmcore\GComponents.h"

REM ============================================================
REM 1. 헤더 파일 비교 (Components.h 와 GComponents.h)
REM ------------------------------------------------------------
echo =====================================================
echo [Step 1] 헤더 파일 비교 시작...
echo 비교: 
echo   ENGINE Components: %ENGINE_COMP%
echo   INSTALL Components: %INSTALL_COMP%
echo   ENGINE GComponents: %ENGINE_GCOMP%
echo   INSTALL GComponents: %INSTALL_GCOMP%
echo ----------------------------------------------------

set "updateMode=none"

REM Components.h 비교 (바이너리 비교)
fc /B "%ENGINE_COMP%" "%INSTALL_COMP%" >nul
if errorlevel 1 (
    echo [INFO] Components.h 내용이 다릅니다.
    set "updateMode=full"
) else (
    REM GComponents.h 비교 (바이너리 비교)
    fc /B "%ENGINE_GCOMP%" "%INSTALL_GCOMP%" >nul
    if errorlevel 1 (
        echo [INFO] GComponents.h 내용이 다릅니다.
        set "updateMode=full"
    ) else (
        echo [INFO] 두 헤더 파일 모두 동일합니다.
    )
)

REM ============================================================
REM 2. 엔진코어(EngineCore) 수정 여부 확인
REM    (헤더 파일이 동일한 경우에만 검사)
REM    여기서는 %~dp0..\EngineCore\Common\Version.cpp 의 수정 날짜를 기준으로 함.
REM ------------------------------------------------------------
if /I "%updateMode%"=="none" (
    echo ----------------------------------------------------
    echo [Step 2] EngineCore 수정 여부 확인: 코드와 DLL의 최신 수정 날짜 비교

    REM BIN 폴더: 최신 DLL들이 위치한 폴더 (예: Release 구성)
    set "BIN_DIR=%~dp0..\bin"
    REM ENGINE_DIR: EngineCore 폴더 전체 하위 폴더 포함
    set "ENGINE_DIR=%~dp0..\EngineCore"

    REM PowerShell을 사용하여 ENGINE_DIR와 BIN_DIR 내의 실제 존재하는
    REM 모든 파일 중에서 가장 최신 수정 시간을 Ticks 단위로 비교합니다.
    for /f "usebackq delims=" %%R in (`powershell -NoProfile -Command "if ((Get-ChildItem -Path '!ENGINE_DIR!' -Recurse -File | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime.ToUniversalTime().Ticks -gt (Get-ChildItem -Path '!BIN_DIR!' -Recurse -Filter *.dll | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime.ToUniversalTime().Ticks) { Write-Output update } else { Write-Output no }"`) do set "COMPARE_RESULT=%%R"

    echo 비교 결과: !COMPARE_RESULT!
    if /I "!COMPARE_RESULT!"=="update" (
        echo [INFO] 코드 EngineCore의 최신 수정일이 DLL의 최신 수정일보다 이후입니다.
        echo 업데이트 필요로 판단되어 VERSION_CPP 만 업데이트하도록 설정합니다.
        set "updateMode=v"
    ) else (
        echo [INFO] DLL이 최신이므로 업데이트가 필요하지 않습니다.
    )
)

echo "%ENGINE_DIR%" 최신 수정 시간 Ticks:
powershell -NoProfile -Command "Get-ChildItem -Path '!ENGINE_DIR!' -Recurse -File | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object { $_.LastWriteTime.ToUniversalTime().Ticks }"

echo "%BIN_DIR%" 최신 DLL 수정 시간 Ticks:
powershell -NoProfile -Command "Get-ChildItem -Path '!BIN_DIR!' -Recurse -Filter *.dll -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object { $_.LastWriteTime.ToUniversalTime().Ticks }"

powershell -NoProfile -Command "Get-ChildItem -Path '%~dp0..\EngineCore' -Recurse -Filter *.* -File | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object { $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fffffff') }"
powershell -NoProfile -Command "Get-ChildItem -Path '%~dp0..\bin\x64_Debug' -Recurse -Filter *.dll -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object { $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fffffff') }"

endlocal

exit /b 0