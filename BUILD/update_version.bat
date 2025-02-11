@echo off
setlocal enabledelayedexpansion

:: -----------------------------------------------------------
:: 0. Git ����� �̸��� "korfriend"���� Ȯ��
:: -----------------------------------------------------------
for /f "delims=" %%G in ('git config user.name') do set "GITUSER=%%G"
if /I not "%GITUSER%"=="korfriend" (
    echo ���� Git �����: %GITUSER%
    echo �� ��ũ��Ʈ�� github ���� "korfriend"������ ����˴ϴ�.
    pause
    exit /b 0
)
echo Git ����� Ȯ�� �Ϸ�: %GITUSER%

:: -----------------------------------------------------------
:: 1. ���� ��� ����
::    ��ġ ������ �ִ� ���͸��� �������� Components.h ���� ��ġ ����
::    (��: ��ġ ���� ��ġ: MyScripts\build.bat, Components.h ��ġ: EngineCore\Components\Components.h)
:: -----------------------------------------------------------
set "COMP_FILE=Components.h"
:: %~dp0 �� ��ġ ������ ���͸�(�������� '\' ����)�� �ǹ���
set "COMP_PATH=%~dp0..\EngineCore\Components\%COMP_FILE%"
echo ������ ����: %COMP_PATH%

:: -----------------------------------------------------------
:: 2. Components.h ���Ͽ��� ���� ���ڿ�("VZ::YYYYMMDD_x") ����
::    PowerShell�� Select-String�� �̿��Ͽ� ���Խ� 'VZ::\d{8}_\d+'�� �´� ���ڿ� �˻�
:: -----------------------------------------------------------
for /f "delims=" %%A in ('powershell -NoProfile -Command "Select-String -Path '%COMP_PATH%' -Pattern 'VZ::\d{8}_\d+' | ForEach-Object { $_.Matches } | ForEach-Object { $_.Value }"') do (
    set "VERSION=%%A"
    goto :found
)
:found
if not defined VERSION (
    echo [%date% %time%] ����: %COMP_PATH% ���Ͽ��� ���� ���ڿ��� ã�� �� �����ϴ�.
    exit /b 1
)
echo ���� ���� ���ڿ�: %VERSION%

:: -----------------------------------------------------------
:: 3. ���� ���ڿ� �и� (������ھ� '_' ����)
::    ��: "VZ::20250211_0" �� datePart="VZ::20250211" , counter="0"
:: -----------------------------------------------------------
for /f "tokens=1,2 delims=_" %%a in ("%VERSION%") do (
    set "datePart=%%a"
    set "counter=%%b"
)
echo ��¥ �κ�: %datePart%
echo ���� ī����: %counter%

:: -----------------------------------------------------------
:: 4. datePart���� "VZ::"�� �����Ͽ� ���� ��¥(YYYYMMDD) ����
:: -----------------------------------------------------------
set "dateOnly=%datePart:~4%"
echo ����� ��¥: %dateOnly%

:: -----------------------------------------------------------
:: 5. ���� ��¥(YYYYMMDD ����) ���ϱ� (PowerShell �̿�)
:: -----------------------------------------------------------
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd"') do set "TODAY=%%i"
echo ���� ��¥: %TODAY%

:: -----------------------------------------------------------
:: 6. ���� ��¥�� ���� �� ��¥ ��
::    - ������ ���� ī���Ϳ� 1 ����
::    - �ٸ��� ��¥�� ���� ��¥�� �����ϰ� ī���͸� 0���� �ʱ�ȭ
:: -----------------------------------------------------------
if "%dateOnly%"=="%TODAY%" (
    set /a newCounter=%counter% + 1
) else (
    set "datePart=VZ::%TODAY%"
    set "newCounter=0"
)
echo �� ī����: %newCounter%

:: -----------------------------------------------------------
:: 7. �� ���� ���ڿ� ����
:: -----------------------------------------------------------
set "newVersion=%datePart%_%newCounter%"
echo �� ���� ���ڿ�: %newVersion%

:: -----------------------------------------------------------
:: 8. Components.h ���� �� ���� ���ڿ� ������Ʈ (PowerShell -replace ���)
:: -----------------------------------------------------------
powershell -NoProfile -Command "(Get-Content '%COMP_PATH%') -replace 'VZ::\d{8}_\d+', '%newVersion%' | Set-Content '%COMP_PATH%'"
echo ���� %COMP_PATH% ��(��) %newVersion%(��)�� ������Ʈ �Ǿ����ϴ�.

:: -----------------------------------------------------------
:: 9. ���� ���� (����: msbuild ���, ȯ�濡 �°� ����)
:: -----------------------------------------------------------
::echo ���带 �����մϴ�...
::msbuild MyProject.sln /p:Configuration=Release

endlocal
pause
