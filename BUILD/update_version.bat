@echo off
setlocal enabledelayedexpansion

:: -----------------------------------------------------------
:: 0. Git 사용자 이름이 "korfriend"인지 확인
:: -----------------------------------------------------------
for /f "delims=" %%G in ('git config user.name') do set "GITUSER=%%G"
if /I not "%GITUSER%"=="korfriend" (
    echo 현재 Git 사용자: %GITUSER%
    echo 이 스크립트는 github 계정 "korfriend"에서만 실행됩니다.
    pause
    exit /b 0
)
echo Git 사용자 확인 완료: %GITUSER%

:: -----------------------------------------------------------
:: 1. 파일 경로 설정
::    배치 파일이 있는 디렉터리를 기준으로 Components.h 파일 위치 지정
::    (예: 배치 파일 위치: MyScripts\build.bat, Components.h 위치: EngineCore\Components\Components.h)
:: -----------------------------------------------------------
set "COMP_FILE=Components.h"
:: %~dp0 는 배치 파일의 디렉터리(마지막에 '\' 포함)를 의미함
set "COMP_PATH=%~dp0..\EngineCore\Components\%COMP_FILE%"
echo 수정할 파일: %COMP_PATH%

:: -----------------------------------------------------------
:: 2. Components.h 파일에서 버전 문자열("VZ::YYYYMMDD_x") 추출
::    PowerShell의 Select-String을 이용하여 정규식 'VZ::\d{8}_\d+'에 맞는 문자열 검색
:: -----------------------------------------------------------
for /f "delims=" %%A in ('powershell -NoProfile -Command "Select-String -Path '%COMP_PATH%' -Pattern 'VZ::\d{8}_\d+' | ForEach-Object { $_.Matches } | ForEach-Object { $_.Value }"') do (
    set "VERSION=%%A"
    goto :found
)
:found
if not defined VERSION (
    echo [%date% %time%] 에러: %COMP_PATH% 파일에서 버전 문자열을 찾을 수 없습니다.
    exit /b 1
)
echo 기존 버전 문자열: %VERSION%

:: -----------------------------------------------------------
:: 3. 버전 문자열 분리 (언더스코어 '_' 기준)
::    예: "VZ::20250211_0" → datePart="VZ::20250211" , counter="0"
:: -----------------------------------------------------------
for /f "tokens=1,2 delims=_" %%a in ("%VERSION%") do (
    set "datePart=%%a"
    set "counter=%%b"
)
echo 날짜 부분: %datePart%
echo 기존 카운터: %counter%

:: -----------------------------------------------------------
:: 4. datePart에서 "VZ::"를 제거하여 실제 날짜(YYYYMMDD) 추출
:: -----------------------------------------------------------
set "dateOnly=%datePart:~4%"
echo 추출된 날짜: %dateOnly%

:: -----------------------------------------------------------
:: 5. 오늘 날짜(YYYYMMDD 형식) 구하기 (PowerShell 이용)
:: -----------------------------------------------------------
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd"') do set "TODAY=%%i"
echo 오늘 날짜: %TODAY%

:: -----------------------------------------------------------
:: 6. 오늘 날짜와 파일 내 날짜 비교
::    - 같으면 기존 카운터에 1 증가
::    - 다르면 날짜를 오늘 날짜로 갱신하고 카운터를 0으로 초기화
:: -----------------------------------------------------------
if "%dateOnly%"=="%TODAY%" (
    set /a newCounter=%counter% + 1
) else (
    set "datePart=VZ::%TODAY%"
    set "newCounter=0"
)
echo 새 카운터: %newCounter%

:: -----------------------------------------------------------
:: 7. 새 버전 문자열 생성
:: -----------------------------------------------------------
set "newVersion=%datePart%_%newCounter%"
echo 새 버전 문자열: %newVersion%

:: -----------------------------------------------------------
:: 8. Components.h 파일 내 버전 문자열 업데이트 (PowerShell -replace 사용)
:: -----------------------------------------------------------
powershell -NoProfile -Command "(Get-Content '%COMP_PATH%') -replace 'VZ::\d{8}_\d+', '%newVersion%' | Set-Content '%COMP_PATH%'"
echo 파일 %COMP_PATH% 이(가) %newVersion%(으)로 업데이트 되었습니다.

:: -----------------------------------------------------------
:: 9. 빌드 실행 (예시: msbuild 사용, 환경에 맞게 수정)
:: -----------------------------------------------------------
::echo 빌드를 시작합니다...
::msbuild MyProject.sln /p:Configuration=Release

endlocal
pause
