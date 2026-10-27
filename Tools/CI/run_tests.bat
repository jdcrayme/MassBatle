@echo off
REM ============================================================
REM run_tests.bat -- MassBattle per-commit automation gate (R8)
REM Usage: run_tests.bat [unit|func]
REM   unit (default): Battle.Unit + Battle.Data + Battle.Smoke, headless editor-cmd
REM   func          : Battle.Func functional tests, standalone -game mode
REM Parses the log for the pass/fail summary (never trusts exit codes alone),
REM writes Tools/CI/last_run.json, and exits nonzero on any test failure.
REM Machine-readable report (UE 5.8: -ReportExportPath, renamed from
REM -ReportOutputPath) is written to Tools\CI\last_run\report\index.json.
REM ============================================================
setlocal enabledelayedexpansion

set TIER=%1
if "%TIER%"=="" set TIER=unit

set ENGINE_DIR=E:\Unreal\UE_5.8
set PROJECT=E:\Unreal Projects\MassBattle\MassBattle.uproject
set THIS_DIR=%~dp0
set RUN_DIR=%THIS_DIR%last_run
set LOG_FILE=%RUN_DIR%\%TIER%.log
set REPORT_DIR=%RUN_DIR%\report

if "%TIER%"=="unit" (
    set FILTER=Battle.Unit+Battle.Data+Battle.Smoke
    set MODE_FLAGS=
) else if "%TIER%"=="func" (
    set FILTER=Battle.Func
    set MODE_FLAGS=-game
) else (
    echo Unknown tier "%TIER%" -- use unit or func
    exit /b 2
)

if not exist "%RUN_DIR%" mkdir "%RUN_DIR%"
if exist "%LOG_FILE%" del "%LOG_FILE%"
if exist "%REPORT_DIR%" rmdir /s /q "%REPORT_DIR%"

echo [run_tests] tier=%TIER% filter=%FILTER% mode=%MODE_FLAGS%
echo [run_tests] log: %LOG_FILE%

"%ENGINE_DIR%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" %MODE_FLAGS% -unattended -nopause -nosplash -nullrhi -abslog="%LOG_FILE%" -ExecCmds="Automation RunTests %FILTER%; Quit" -TestExit="Automation Test Queue Empty" -ReportExportPath="%REPORT_DIR%"
set ENGINE_EXIT=%ERRORLEVEL%
echo [run_tests] engine exit code: %ENGINE_EXIT%

python "%THIS_DIR%summarize_run.py" --tier %TIER% --log "%LOG_FILE%" --report "%REPORT_DIR%\index.json" --out "%THIS_DIR%last_run.json"
set SUMMARY_EXIT=%ERRORLEVEL%

REM Audit L3: the engine exit code is part of the verdict -- a nonzero
REM engine exit with a parseable log must not pass the gate.
if not "%ENGINE_EXIT%"=="0" if %SUMMARY_EXIT%=="0" (
    echo [run_tests] FAIL: engine exit %ENGINE_EXIT% with a clean summary ^(audit L3^)
    endlocal & exit /b 1
)
endlocal & exit /b %SUMMARY_EXIT%
