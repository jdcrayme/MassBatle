@echo off
REM ============================================================
REM nightly.bat -- MassBattle nightly orchestration (R8)
REM 1. Build the editor target (catches code regressions first)
REM 2. Build + cook + stage the Win64 game via RunUAT BuildCookRun
REM 3. Run the staged build with the Gauntlet smoke controller
REM    (-gauntlet=BattleSmoke; audit note: 5.8's UAT verb is RunUnreal and it
REM    requires project-side Gauntlet test-node assemblies, so the single-machine
REM    nightly drives the native game-side framework directly and checks the
REM    process exit code + log -- deviation recorded in ADR-16)
REM 4. Append the baseline JSON (machine spec embedded, R10) and compare
REM 5. git commit the baseline
REM ============================================================
setlocal enabledelayedexpansion

set ENGINE_DIR=E:\Unreal\UE_5.8
set PROJECT=E:\Unreal Projects\MassBattle\MassBattle.uproject
set PROJ_DIR=E:\Unreal Projects\MassBattle
set THIS_DIR=%~dp0
set NIGHT_DIR=%THIS_DIR%last_nightly
set STAGE_DIR=%PROJ_DIR%\Saved\StagedBuilds

if not exist "%NIGHT_DIR%" mkdir "%NIGHT_DIR%"
set START_TS=%DATE% %TIME%

echo [nightly 1/5] Building editor target...
call "%ENGINE_DIR%\Engine\Build\BatchFiles\Build.bat" MassBattleEditor Win64 Development -Project="%PROJECT%" -WaitMutex
if errorlevel 1 (
    echo [nightly] editor build FAILED
    exit /b 1
)

echo [nightly 2/5] Packaging Win64 (BuildCookRun -cook -stage -pak)...
call "%ENGINE_DIR%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT%" -platform=Win64 -build -cook -stage -pak -unattended -nop4
if errorlevel 1 (
    echo [nightly] packaging FAILED
    exit /b 1
)

set GAME_EXE=%STAGE_DIR%\Windows\MassBattle\Binaries\Win64\MassBattle.exe
if not exist "%GAME_EXE%" (
    echo [nightly] staged exe not found: %GAME_EXE%
    dir /b /s "%STAGE_DIR%\*.exe"
    exit /b 1
)

echo [nightly 3/5] Running Gauntlet smoke on staged build...
set GAUNTLET_LOG=%NIGHT_DIR%\gauntlet_smoke.log
if exist "%GAUNTLET_LOG%" del "%GAUNTLET_LOG%"
"%GAME_EXE%" -unattended -nosplash -nullrhi -nopause -abslog="%GAUNTLET_LOG%" -gauntlet=BattleSmoke -battlesmoke.wait=20
set GAUNTLET_EXIT=%ERRORLEVEL%
echo [nightly] gauntlet exit code: %GAUNTLET_EXIT%
findstr /c:"GAUNTLET BattleSmoke" "%GAUNTLET_LOG%"
findstr /c:"TEST COMPLETE. EXIT CODE: 0" "%GAUNTLET_LOG%" >nul
if errorlevel 1 (
    echo [nightly] gauntlet smoke FAILED ^(no clean TEST COMPLETE^)
    exit /b 1
)

echo [nightly 4/5] Writing candidate baseline + comparing against the committed reference...
REM Audit H2 fix: the nightly writes a CANDIDATE under last_nightly/ (untracked)
REM and compares it against the COMMITTED reference. The reference is promoted
REM (overwritten + committed) only when the compare passes, so a regression
REM fails the nightly and leaves the reference untouched.
set REF_BASELINE=%PROJ_DIR%\Baselines\perf\ref-i7-9700F-1660Ti\nightly-smoke.json
set CAND_BASELINE=%NIGHT_DIR%\candidate-nightly-smoke.json
python "%THIS_DIR%write_baseline.py" --gate nightly-smoke --smoke-log "%GAUNTLET_LOG%" --out "%CAND_BASELINE%" --seed-from "%REF_BASELINE%"
if errorlevel 1 (
    echo [nightly] baseline write FAILED
    exit /b 1
)

if not exist "%REF_BASELINE%" (
    echo [nightly] no committed reference yet -- promoting first candidate as the baseline
    copy /y "%CAND_BASELINE%" "%REF_BASELINE%" >nul
    goto baseline_commit
)

python "%THIS_DIR%compare_baseline.py" --baseline "%REF_BASELINE%" --candidate "%CAND_BASELINE%" --tolerance 0.5
if errorlevel 1 (
    echo [nightly] BASELINE REGRESSION -- reference NOT updated; candidate kept at %CAND_BASELINE%
    exit /b 1
)

copy /y "%CAND_BASELINE%" "%REF_BASELINE%" >nul

:baseline_commit
echo [nightly 5/5] Committing baseline...
cd /d "%PROJ_DIR%"
git add Baselines/perf
git commit -m "phase0: nightly baseline update (automated)" --no-verify
if errorlevel 1 echo [nightly] note: nothing to commit (non-fatal)

echo [nightly] DONE started=%START_TS% ended=%DATE% %TIME%
endlocal
