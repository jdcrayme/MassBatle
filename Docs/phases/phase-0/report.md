# MassBattle Phase 0 Handoff Report

> **Archive note (2026-10-25 reorg).** Phase 0 handoff report (was `Docs/agent/phase0-report.md`). §7 "Where Phase 1 should start" remains the live entry point for the next phase. Current status: `Docs/README.md`.

**Date:** 2026-10-24 · **Agent run:** autonomous Phase 0 per `Docs/phases/phase-0/brief.md` v2.0 (owner-approved A1–A7)
**Engine:** UE 5.8.4 (CL 58902052, `++UE5+Release-5.8`) at `E:\Unreal\UE_5.8` · Project: `E:\Unreal Projects\MassBattle\MassBattle.uproject`
**Status:** **All tasks 0–13 complete, DoD checked. No open blockers.** Evidence verbatim below; every claim has a log line, test JSON, or git hash.

---

## 1. What was built (paths)

### R8 CI foundation (Task 1 — owner's #1 priority, landed first)
- `Tools/CI/run_tests.bat` — per-commit gate (tiers `unit` / `func`), headless `UnrealEditor-Cmd`, log-parse + `-ReportExportPath` JSON (cross-checked), writes `Tools/CI/last_run.json`
- `Tools/CI/summarize_run.py` — pass/fail summary parser (stdlib; reads UE's UTF-8-BOM report)
- `Tools/CI/nightly.bat` + `Tools/CI/write_baseline.py` — editor build → `RunUAT BuildCookRun -cook -stage -pak` → staged exe with `-gauntlet=BattleSmoke` → baseline append → compare → git commit
- `Tools/CI/compare_baseline.py` — tolerance compare (per-metric overrides, pending/new handling)
- `Baselines/perf/README.md` + `Baselines/perf/ref-i7-9700F-1660Ti/{gate-unit,nightly-smoke}.json` — 2 files, real metrics (`unit_tier_wall_s=64.558`, `smoke_test_duration_s=21.824`, `entity_spawn_80_us=35.1`), machine spec embedded (R10)
- `Jenkinsfile` + `Tools/CI/JENKINS.md` — Jenkins LTS **2.580.1** installed portable at `E:\Tools\jenkins` (Temurin JDK 21 zip + war, localhost-only, admin + API token via init-groovy bootstrap; no MSI/service). Jobs: `massbattle-gate` (pipeline-from-SCM, **ran green: build 3 SUCCESS**) and `massbattle-nightly` (cron `H 2 * * *` → `nightly.bat`). ADR-16 recorded.
- `Plugins/.../BattleSmokeController.{h,cpp}` — Gauntlet stub (`-gauntlet=BattleSmoke`), proven on the staged build (caught the GameFeatureData cook error before any human saw it)
- `Config/DefaultEngine.ini` — `[Zen.AutoLaunch] LimitProcessLifetime=false`; `Config/DefaultGame.ini` — GameFeatureData AssetManager rule (cook fix, see §3.2)

### Data spine (Tasks 3–6)
- `Public/BattleTypes.h` — `FUnitHandle` (index+serial, GetTypeHash), `FBattleAttributeSnapshot`, `FUnitRuntimeState`, `FUnitSpawnSpec`
- `Public/BattleFragments.h` — `FOwnerUnitFragment`, `FLoadoutFragment`, `FBaseStatsFragment`, `FHealthFragment`, `FUnitStatsSharedFragment` (ADR-14 anchor first), tags (`FInfantryTag`…), `FDeadTag` (**regular** tag — ADR-15 final state, §3.1)
- `Public/BattleRegistry.h` + `BattleExternalSubsystemTraits.h` — `UUnitRegistrySubsystem`: per-slot generation counters, LIFO free-list, roster bookkeeping, idempotent dirty flags, sorted-handle dump, bookkeeping hash; traits `GameThreadOnly=false, ThreadSafeWrite=false`
- `Public/BattleEduParser.h/.cpp` — pure 14-field parser (schema read from the fixture), strict tokenizer, loud unknown-field rejection, canonical serializer (rules in header)
- `Public/BattleSpawnLibrary.h/.cpp` — Route-A batched spawn: archetype-first (`CreateArchetype` + `FMassArchetypeSharedFragmentValues` + `BatchCreateEntities`), ADR-14 anchored shared fragment, fixup pass, roster publish, `EXEC_RESULT SpawnPerf`
- `Public/BattleUnitTrait.h/.cpp` + `Battle.BuildT0Configs` — A5 route: configs authored in C++ (`UMassEntityConfigAsset` + composition trait), saved to `/Game/MassBattle/Data/Units/M_Battle_{PeasantLevy,SergeantSpearmen}` (committed)
- `Content/MassBattle/Test/Maps/L_MassBattle_T0_Functional.umap` — S-scenario map (MCP-authored, pruned template, Ground ×256, saved clean)

### Translation layer (Tasks 7–9)
- `Public/UnitActor.h/.cpp` — `AUnitActor` (ASC + closed 11-attribute `UUnitAttributeSet`, `ATTRIBUTE_ACCESSORS_BASIC`), `InitAbilityActorInfo`, attribute delegates → **snapshot write + dirty flag** (arch §5.1), owns spawn, death-event observation
- `Public/UnitSyncProcessor.h/.cpp` — pipeline row 0 (PrePhysics, group `Battle`, `AllWorldModes`, game-thread); dirty flags → shared-fragment multipliers
- `Public/BattleDeathPath.h/.cpp` — `UBattleDeathObserver` (observes `FDeadTag` AddElement **during the kill flush** — the E5 audited point) → deferred `Battle.UnitDeath` signal → `UBattleDeathSignalReceiver` (roster update + `SendGameplayEventToActor` "Battle.Unit.Died" → deferred destroy). ADR-15 final state: regular tag; full probe story in §3.1 + audit C10/C11
- ExecBridge: `Battle.DumpRegistry`, `Battle.StateHash`, `Battle.SpawnRoster`, `Battle.BuildT0Configs`, `Battle.SetAttr`, `Battle.KillEntity`, `Battle.DumpSharedFrag`, `Battle.DumpMetrics` (all `EXEC_RESULT`-formatted)

### Determinism + test contract (Tasks 10–11)
- `Public/BattleStateHash.h/.cpp` — CityHash64 over registry fold + quantized per-entity state + archetype count + shared values; no wall-clock/frame counters
- `Public/BattleRngRegistry.h/.cpp` — `FBattleRngRegistry` (pure) + thin subsystem; `HashCombine(root, GetTypeHash(name))` per named stream
- `Public/BattleTestSettings.h/.cpp` — `Battle.Test.{FixedStep,DisableTimeSlicing,ForceFullDetail}` + `ApplyTestMode()` / `AssertTestMode()` (reads ACTIVE engine state)

### Debug overlay skeleton (Task 12, hooks only per A1)
- `Public/BattleDebugSubsystem.h/.cpp` — `Battle.Debug.{ShowOverlay,Enabled}` CVars, B.4 metrics format (`EXEC_RESULT BattleMetrics Frame Archetypes Units UnitSyncMs BookkeepingHash OverlayHook`), UnitSync ms shim, `Battle.DumpMetrics`; MassInsights I2 verification in audit §12 (engine-side wiring; `-trace=` path documented)

### Docs
- `Docs/reference/engine-api-5.8.md` — I6 audit: corrections **C1–C12**, all BRIEF §4 ⚠️ resolved; T0.9 settings section; E5 flush contract; MassInsights check
- `Docs/phases/phase-0/progress.md` — step-by-step log with verification per step
- `Docs/spec/architecture.md` — ADR-15/16 rows; §4 5.8-signature note (A7)
- `Docs/spec/roadmap-and-testing.md` — exit-criteria + T0.5 probe annotations, B.0 test-mode reference, B.7 Phase 0 entry (A7)
- `Docs/phases/phase-0/brief.md` — §1/§4 facts corrected in place (C1–C8)

## 2. Test evidence (verbatim, condensed)

**Per-commit tier — headless `run_tests.bat unit` (final, Task 13):**
```
[summarize] tier=unit passed=15 failed=0 total=15 completed=True exit=0 ok=True
```
15 tests: `Battle.Data.{EduEdgeCases,EduRoundTrip,EntityConfigIntegrity}`, `Battle.Smoke.PluginLoaded`, `Battle.Unit.AttributeSet.{ClosedSet,DelegateAndDirtyFlag}`, `Battle.Unit.Mass.SharedFragUniqueness`, `Battle.Unit.Registry.{DirtyFlags,ExecProbes,FreeListReuseSafety,RoundTrip10k,StaleHandles}`, `Battle.Unit.Rng.Discipline`, `Battle.Unit.StateHash.Stability`, `Battle.Unit.TestMode.Contract`.

**Functional tier — headless `run_tests.bat func` (final):**
```
[summarize] tier=func passed=3 failed=0 total=3 completed=True exit=0 ok=True
```
- T0.4/T0.8: `EXEC_RESULT S0_SyncLatency Ticks=2 A=1.5000 B=1.0000` (sync ≤ 1 frame; B isolated)
- T0.5: `EXEC_RESULT DeathObserver Signaled=1` → `DeathSignal Entity=6:1 Unit=1:1 RosterIndex=5 GAS=1` → `S0_DeathPath Tick=2 Tagged=1 Roster=7 GAS=1` → destroy completed
- Task 13 exit criteria: `S0_ExitCriteria SyncTick=2 A=1.5000 B=1.0000` + `DeathTick=3 Roster=39 Tagged=1 GAS=1 HashBefore=… HashAfter=…` (hashes differ)
- T0.1/T0.2/T0.3/T0.6/T0.7/T0.9: all `state: Success` in the unit-tier JSON (editor session and headless runs agree; report counts cross-checked)

**Spawn perf:** `EXEC_RESULT SpawnPerf Unit=1:1 Micros=23.0 PerEntity=0.57` / `Unit=2:1 Micros=12.1 PerEntity=0.30` (2×40). Baseline: `entity_spawn_80_us=35.1`.

**Jenkins:** gate pipeline build 3 = SUCCESS (checkout → UBT → unit gate). Nightly: full pipeline green (cook → stage → Gauntlet `PASS -- clean 20.0s boot, 0 errors` → baseline `21.824s` → compare PASS → auto-commit `a74b8f1`).

**Git:** one commit per task: `c374317` kickoff · `43ee97b`+`a74b8f1` CI · `3a3eec7` audit · `03a4179` spine · `fa7f924` registry · `f86d44f` EDU · `958c06d` spawn+GAS · `8e72b9d` UnitSync/T0.4 · `1bc495f` death/hash/rng/testmode/debug · `7337d98` exit criteria + baselines · final `phase0: complete`.

## 3. Facts corrections & deviations (reality wins — full list in audit C1–C12 + progress log)

1. **ADR-15 reverted by the on-machine probe (the phase's biggest finding).** 5.8.4 sparse tags: (C10) observers reject sparse elements (`MassObserverRegistry.cpp:25` ensure — crashed -game boot); (C11) sparse **tags are chunk-granular** (`MassSparseElementsStorage.h`: per-chunk bitset, "no data per instance") — `AddTag` on ONE entity marked all 8 same-chunk entities dead. Per-entity death marking requires the regular tag. `FDeadTag` is a regular `FMassTag`; the owner-approved "adopt sparse" decision could not survive contact with the engine; ADR-15 records the probe. The churn concern is deferred to Phase 2 (per-unit archetypes localize flips).
2. **ADR-16 (CI host):** Jenkins LTS as decided — but 5.8's UAT verb is `RunUnreal` and requires project-side Gauntlet test-node assemblies, so the nightly drives the native game-side Gauntlet framework on the staged build directly (exit code + `TEST COMPLETE` log). Deviation recorded in ADR-16.
3. **Cook fix:** `GameFeaturesToolset` (MCP plugins) activates the GameFeatures plugin; cooking requires an AssetManager GameFeatureData rule — in **DefaultGame.ini** (`UAssetManagerSettings` is `config=Game`) with `bIsEditorOnly=False` for runtime visibility. Two cook failures diagnosed and fixed by the nightly on its first runs.
4. **RegisterWithProcessor still exists** in 5.8 (BRIEF said removed) — ctor registration is the pattern anyway (C1). Signal receivers: no `ConfigureSignals`/`SignalReceiveDelegate`; use `SubscribeToSignal` + `SignalEntities` override (C2). `SetFixedTimeStepValue` → `FApp::SetFixedDeltaTime` (C4). `-ReportOutputPath` → `-ReportExportPath` (C5, writes BOM'd index.json). `GetOrCreateSharedFragmentByHash` is **private** (C8) — public content-CRC create / archetype-value attach used. Base structs live in MassCore and need explicit linking (C12). Sparse realities: C10/C11.
5. **Task 6+7 share one commit** (`958c06d`) — the actor's spawn path and the spawn library landed together (file coupling); all other tasks are one-commit.
6. **Functional-harness shape** (documented in code): -game class resolution needs a static factory registry (reflection unreliable); `PrepareTest` is the manager entry point; `FinishTest` requires the manager (manual completion); the T0.4 assert lands at tick N+1 (Mass PrePhysics for the set-frame already ran — exactly the accepted 1-frame latency, arch §5.1).

## 4. Tool quirks (gold for future runs)

1. MCP: `tools/call` is a meta-tool (`call_tool` with `toolset_name`/`tool_name`/`arguments`); `ListTests` requires `tagFilter` (pass `""`); responses are double-nested JSON; **no console-execution toolset** — ExecBridge commands run from tests (`World->Exec` also does NOT reach `FAutoConsoleCommandWithWorldAndArgs`; drive the `BattleExec::` functions or run headless).
2. UBT cannot run while the editor is open ("Live Coding is active" even with Live Coding off) — cycle: edit → kill editor → UBT → relaunch → re-DiscoverTests. ~6 editor launches this phase.
3. `-ExecCmds` in editor-cmd mode does not reliably reach world-arg console commands; automation (`Automation RunTests`) parses the flag itself — that's why the gate works.
4. Adaptive unity builds merge test TUs — per-file anonymous-namespace helper names must be unique (MakeWorld collided twice).
5. Jenkins 2.580: anonymous admin POSTs blocked even with the wizard off; crumbs are session-bound (scripts need the API token); git plugin needs `ALLOW_LOCAL_CHECKOUT=true` for `file:///` remotes; parameterized jobs need `/buildWithParameters`.
6. `run_tests.bat` evidence: log-parse (per-test `Test Completed. Result={…}` lines) cross-checked against `index.json` top-level counts; `succeededWithWarnings` (benign GameplayCueNotifyPaths hint) counts as pass on the log side.

## 5. Open blockers

None.

## 6. Definition of done (§7 checklist)

- [x] All T0.1–T0.9 green — evidence JSON/log lines captured (§2 + `Tools/CI/last_run/`)
- [x] Exit criteria: 2×40 from table data; attribute change visible ≤ 1 frame (tick-counted); kill → tag → signal → roster update (S0_ExitCriteria green)
- [x] R8 CI: gate script + Jenkins LTS (ADR-16) + nightly packaged job incl. Gauntlet stub + baseline storage (2 files, 3 real metrics) + compare script + Zen config
- [x] ADR-15 recorded (probe outcome: regular tag) ; ADR-16 recorded (Jenkins portable + native-Gauntlet nightly)
- [x] `Docs/reference/engine-api-5.8.md` complete (C1–C12; every §4 ⚠️ resolved)
- [x] A7 patches: arch §4 signature note; roadmap T0.5/exit-criteria annotations (redirected — original wording correct again); roadmap B.0 test-mode reference
- [x] `Docs/phases/phase-0/progress.md` complete; `phase0: complete` commit exists
- [x] B.8 per capability: unit tests ✓ · scenario tests (S0_*) ✓ · debug hooks ✓ · metrics (BattleMetrics) ✓ · B.7 regression entry ✓ · `Battle.*` cvar flags ✓ (registry/EDU/spawn/sync/death each checked)
- [x] Editor left open on `L_MassBattle_Demo01`, both maps `is_dirty=false`, MCP responsive (final checks this session)

## 7. Where Phase 1 should start

1. Read `Docs/reference/engine-api-5.8.md` first (12 corrections; the §11 T0.9 section and §3 flush contract are load-bearing), then `Docs/phases/phase-0/progress.md` for the functional-harness shape (S0 tests + static factory registry + `PrepareTest`/manual-completion lifecycle).
2. **Movement core** (roadmap Phase 1) plugs into ready hooks: `FUnitStatsSharedFragment` is written by UnitSync every frame; per-unit archetypes localize death flips; `UBattleRngSubsystem` provides named streams; `FBattleStateHash` extends with transform terms (1 cm quantization) for T1 gates.
3. The death path currently destroys corpses immediately (receiver-deferred) — Phase 5 corpse policy replaces it; the R1 churn concern (mass-death archetype flips) should re-evaluate chunk layout when roster sizes grow (per-unit archetypes already bound the blast radius to one unit).
4. Jenkins nightly is wired (`massbattle-nightly`, cron 02:00) but Jenkins must be RUNNING for the cron to fire — start line in `Tools/CI/JENKINS.md`; optionally add a logon Task Scheduler entry (process supervision only, still ADR-16).
5. The spawn metric baseline (`entity_spawn_80_us=35.1`) came from the editor-session unit tier; re-baseline on the nightly hardware path when the first real perf gate lands (T2.9).
