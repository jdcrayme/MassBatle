# Phase 3 -- Ranged Stack: Progress Log

| | |
|---|---|
| **Session** | 2026-10-28, kickoff-artifact pass + Task 0 machine verification (single autonomous run, protocol per `Docs/AGENTS.md`) |
| **Engine** | UE 5.8.4 CL 58902052 at `E:\Unreal\UE_5.8` (reference machine `ref-i7-9700F-1660Ti`) |
| **Project** | `E:\Unreal Projects\MassBattle` (plugin module `MassBattle`, test module `MassBattleTest`) |
| **Kickoff state** | Patch series `0010..0016` applied from the design-pass bundle, statically verified + corrected, landed on `main` @ `9be490d` (fixes F-K1..K6: see `Docs/phases/phase-3/kickoff-verification.md`) |

## Discrepancies (dated, newest first)

- **2026-10-28 (Task 0, run 1)** -- `Tools/CI/run_tests.bat` does NOT build: it launches
  `UnrealEditor-Cmd` on existing binaries. Run directly, it executed the pre-kickoff DLLs
  against the new EDU fixture -> 11 spurious failures, all `unknown field 'stat_sec'
  (schema has 14 fields)`. The Jenkinsfile builds `MassBattleEditor` FIRST (Build.bat ->
  run_tests.bat). Task 0 now follows that order; consider a build guard in the bat
  (owner call, logged not applied -- out of Task 0 scope).
- **2026-10-28 (Task 0, build)** -- First real compile of the engine in this workspace hit
  a stale rules-assembly blocker: `UE5Rules.dll` (precompiled, "File is installed")
  predated the Fab plugin's `Engine/Plugins/Fab` install, so `FabToolset` module rules did
  not exist and UBT aborted (`RulesError`). The makefile cache had masked it since the
  last clean rules build. Fix: delete `UE5Rules.{dll,pdb}` + `UE5RulesManifest.json`,
  lift `Engine/Build/InstalledBuild.txt`, run `-Mode=QueryTargets` (regenerates the
  rules assemblies incl. Fab), restore the marker. Recorded in
  `Docs/reference/environment.md` §4; blocks EVERY source change until it bites once.
- **2026-10-28 (Task 0, build)** -- F-K7 (compile slips in the kickoff series, found by
  the first real build): 7 `TestEqual` calls in `MassBattleRangedTests.cpp` passed
  LWC-double `FVector` components against float literals -> MSVC C2666
  (float/double overload ambiguity). Fixed on main (double literals at those sites).
- **2026-10-28 (Task 0, run 2)** -- F-K8 (runtime test defects, found by the first run
  with fresh binaries): (a) inline EDU fixtures in `MassBattleGarrisonTests.cpp` and
  `BattleFuncTestActor.cpp` lacked `stat_sec` -> rejected by the 15-key schema (the
  kickoff's re-sign sweep missed them; all four test files now carry it); (b) the
  ballistics `StepFlight` helper truncated the final partial step, banding landings by
  up to v*DT (1 m at bolt speed) vs the 0.35 m golden tolerance -> final partial step
  simulated; (c) the LOS golden "11 m mid-hill blocks" was knife-edge under the march's
  bilinear + entry-station-per-cell sampling (measured window 8-11 m clear, 12 m
  blocks) -> golden moved to 12 m with the reason in a comment; (d) two
  `VolleySelection` blocks reused a shooter view across sub-cases -- a fired shot arms
  the reload timer and poisons later cases (the two "hold" asserts passed for the
  wrong reason) -> fresh shooter per case. Unit tier re-run: 75/75.

## Task 0 -- kickoff patch application + verification

- [task0.1] Patches `0010..0016` applied (git am, clean), statically verified,
  corrected in-branch (F-K1..K6), landed on `main` -> HEAD `9be490d` before machine
  work began; series exported as `Patches/0010..0016.patch` + record.
- [task0.2] Build `MassBattleEditor Win64 Development` -> **Succeeded** (after the
  rules-assembly recovery + F-K7 fix).
- [task0.3] Unit tier (`run_tests.bat unit`): **passed=75 failed=0 total=75
  completed=True exit=0 ok=True** (`Tools/CI/last_run.json`, run 3). New suites all
  green: `Battle.Unit.Ranged.{Ballistics, LOS, Pool, AmmoAccounting, VolleySelection,
  SkirmishMath}`; re-signed goldens green: `Battle.Data.EduRoundTrip` (15 keys, 4
  fixture units), `Battle.Unit.Combat.{SpawnComposition, HashTerms, SaveRoundTrip}`,
  `Battle.Unit.Garrison.Booking`, `Battle.Data.EntityConfigIntegrity`,
  `Battle.Unit.AttributeSet.DelegateAndDirtyFlag`.
- [task0.4] Archetype probe (SpawnComposition AddInfo, verbatim):
  `archetypes: melee-only=3 with-ranged=4 (growth 1, one ranged composition expected;
  itemized in the phase report)`. Isolated-world probe: +1 live composition (arrow and
  bolt share it -- Kind is a fragment value). The F6 alive/dead 6 -> 8 count is
  asserted at scenario/soak level (Tasks 6/7 gates: soak FINAL line `archetypes=8`).
- [task0.5] Func tier (`run_tests.bat func`): **passed=19 failed=4 total=23**. RED on
  the two fail-by-design skeletons (counted = 2): `Battle.Func.S6_Volley`,
  `Battle.Func.SkirmishBehavior` -- both fail via the harness's M17 tick budget
  (36001 ticks) because the skeleton actors complete before the harness's watch window
  opens; Task 6 replaces them. ADDITIONALLY red: `S4_ShieldWall` + `S5_Mob`
  (`FAIL no pull-down (240 s; exposed=3 waiters=44)`) -- the two KNOWN phase-2
  deferred melts (`Docs/phases/phase-2/audit-engagement-verification.md` "Still
  deferred: S4_ShieldWall + S5_Mob"); they fail at base `48f1553` identically, so they
  are NOT kickoff regressions. No other func regressions.

## Open items carried

- S4_ShieldWall / S5_Mob melts: pre-existing, owned by the phase-2 dossier's deferred
  decision; untouched this phase.
- Skeleton-red func failures: by design until Task 6.
