@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM 0. �⺻ ���� ����
REM ------------------------------------------------------------
REM %~dp0 : ���� ��ġ ������ �ִ� ���͸� (������ '\' ����)
set "ENGINE_COMP=%~dp0..\EngineCore\Components\Components.h"
set "ENGINE_GCOMP=%~dp0..\EngineCore\Components\GComponents.h"
set "INSTALL_COMP=%~dp0..\Install\vzmcore\Components.h"
set "INSTALL_GCOMP=%~dp0..\Install\vzmcore\GComponents.h"

REM ============================================================
REM 1. ��� ���� �� (Components.h �� GComponents.h)
REM ------------------------------------------------------------
echo =====================================================
echo [Step 1] ��� ���� �� ����...
echo ��: 
echo   ENGINE Components: %ENGINE_COMP%
echo   INSTALL Components: %INSTALL_COMP%
echo   ENGINE GComponents: %ENGINE_GCOMP%
echo   INSTALL GComponents: %INSTALL_GCOMP%
echo ----------------------------------------------------

set "updateMode=none"

REM Components.h �� (���̳ʸ� ��)
fc /B "%ENGINE_COMP%" "%INSTALL_COMP%" >nul
if errorlevel 1 (
    echo [INFO] Components.h ������ �ٸ��ϴ�.
    set "updateMode=full"
) else (
    REM GComponents.h �� (���̳ʸ� ��)
    fc /B "%ENGINE_GCOMP%" "%INSTALL_GCOMP%" >nul
    if errorlevel 1 (
        echo [INFO] GComponents.h ������ �ٸ��ϴ�.
        set "updateMode=full"
    ) else (
        echo [INFO] �� ��� ���� ��� �����մϴ�.
    )
)

REM ============================================================
REM 2. �����ھ�(EngineCore) ���� ���� Ȯ��
REM    (��� ������ ������ ��쿡�� �˻�)
REM    ���⼭�� %~dp0..\EngineCore\Common\Version.cpp �� ���� ��¥�� �������� ��.
REM ------------------------------------------------------------
if /I "%updateMode%"=="none" (
    echo ----------------------------------------------------
    echo [Step 2] EngineCore ���� ���� Ȯ��: �ڵ�� DLL�� �ֽ� ���� ��¥ ��

    REM BIN ����: �ֽ� DLL���� ��ġ�� ���� (��: Release ����)
    set "BIN_DIR=%~dp0..\bin"
    REM ENGINE_DIR: EngineCore ���� ��ü ���� ���� ����
    set "ENGINE_DIR=%~dp0..\EngineCore"

    REM PowerShell�� ����Ͽ� ENGINE_DIR�� BIN_DIR ���� ���� �����ϴ�
    REM ��� ���� �߿��� ���� �ֽ� ���� �ð��� Ticks ������ ���մϴ�.
    for /f "usebackq delims=" %%R in (`powershell -NoProfile -Command "if ((Get-ChildItem -Path '!ENGINE_DIR!' -Recurse -File | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime.ToUniversalTime().Ticks -gt (Get-ChildItem -Path '!BIN_DIR!' -Recurse -Filter *.dll | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime.ToUniversalTime().Ticks) { Write-Output update } else { Write-Output no }"`) do set "COMPARE_RESULT=%%R"

    echo �� ���: !COMPARE_RESULT!
    if /I "!COMPARE_RESULT!"=="update" (
        echo [INFO] �ڵ� EngineCore�� �ֽ� �������� DLL�� �ֽ� �����Ϻ��� �����Դϴ�.
        echo ������Ʈ �ʿ�� �ǴܵǾ� VERSION_CPP �� ������Ʈ�ϵ��� �����մϴ�.
        set "updateMode=v"
    ) else (
        echo [INFO] DLL�� �ֽ��̹Ƿ� ������Ʈ�� �ʿ����� �ʽ��ϴ�.
    )
)

echo "%ENGINE_DIR%" �ֽ� ���� �ð� Ticks:
powershell -NoProfile -Command "Get-ChildItem -Path '!ENGINE_DIR!' -Recurse -File | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object { $_.LastWriteTime.ToUniversalTime().Ticks }"

echo "%BIN_DIR%" �ֽ� DLL ���� �ð� Ticks:
powershell -NoProfile -Command "Get-ChildItem -Path '!BIN_DIR!' -Recurse -Filter *.dll -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object { $_.LastWriteTime.ToUniversalTime().Ticks }"

powershell -NoProfile -Command "Get-ChildItem -Path '%~dp0..\EngineCore' -Recurse -Filter *.* -File | Where-Object { Test-Path $_.FullName } | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object { $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fffffff') }"
powershell -NoProfile -Command "Get-ChildItem -Path '%~dp0..\bin\x64_Debug' -Recurse -Filter *.dll -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object { $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fffffff') }"

endlocal

exit /b 0