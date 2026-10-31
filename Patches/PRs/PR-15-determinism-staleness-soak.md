# PR-15: Determinism (T4.7), staleness bound (T4.8), the SoakRout variant, and the re-baselines

| | |
|---|---|
| **Finds** | Phase-4a brief Task 8 (spec morale-and-gas sections 4.8, MC1/MC10): the deterministic pin is PR-12's mechanism; the GATES that prove it (the two-run extension, the staleness fixture, the soak variant, the baselines) are this PR's runtime evidence |
| **Blocks on** | PR-12 + PR-13 executed; Task 1's blocker disposition (the mixed two-run equality is the SAME gate T4.7 extends -- if Task 1 fixes the servo transient, T4.7 runs on a green base; if it escalates, T4.7's morale-term delta is still provable on the melee-only worlds) |
| **Owner** | The T2.9-pattern re-baseline decisions (record vs re-sign) + the nightly schedule slot for the SoakRout staged run |

## Motivation

MC10 demands two-run hash equality on a mixed battle INCLUDING morale, effects, routs, and rallies; MC1 demands the staleness bound under load. The mechanism ships with PR-12 (the `DeterministicTick` pin); the proof needs the extended gate + the loaded fixture + the nightly soak variant -- all machine-gated. The baseline refresh closes the phase's perf bookkeeping (the bOfficer spawn field may move `entity_spawn_80_us`; the morale pass adds `MoraleMs`).

## Proposed change

1. `DeterminismTwoRun` extended (the phase-3 mixed-battle extension pattern): the fixture gains a general + an impetuous row + a scripted volley that breaks one unit and rallies it under the aura; hash every 60 ticks for 40 s under `Battle.Morale.DeterministicTick=1`; assert mismatches=0. The morale terms are in block 10 + the fragment multipliers (the catalog writes `*Mod`), so the equality covers the full loop.
2. The T4.8 staleness fixture: the `Battle.Ranged.PerfStress` shape (loaded world, 2,400 entities) with the morale pass on; sample `TickAgeMax` across a 60 s window; assert `<= Battle.Morale.TickPeriod` (MC1's bound). Record the `MoraleMs` line in the baseline notes (the T2.9 pattern -- record, don't gate).
3. `Battle.Morale.SoakRout=1` (the SoakVolley precedent): the soak controller's spawn mix gains a general + impetuous rows issuing the scripted break/re-cover cycle; the FINAL line APPENDS `routs=%d rallies=%d shattered=%d officerDeaths=%d moraleTicks=%d` (the F14 positional rule -- append, never reorder); flag off = byte-identical baseline.
4. Baselines: re-measure `entity_spawn_80_us` with the bOfficer field (re-sign only if moved beyond noise, disclosed); the nightly runs the SoakRout staged variant once as the phase's soak evidence (rides Task 2's unblock).

## Files

- Edit: `BattleFuncTestActor.cpp` (the DeterminismTwoRun extension), `BattleMovementSoakController.cpp` (the SoakRout variant + the appended counters), `Baselines/perf/` (the re-signed spawn baseline if moved; the MoraleMs/TickAgeMax notes)
- New: the staleness fixture rides `BattleExecCommands.cpp` (`Battle.Morale.StalenessProbe` or an argument on the perf fixture -- the T3.6 shape)

## Evidence to gather on the UE machine

- `DeterminismTwoRun` (morale-extended): PASS with mismatches=0 (or the Task-1-dossier relationship recorded if the pre-existing blocker stands -- the morale terms must not ADD a divergence class)
- The staleness line: `EXEC_RESULT MoraleStaleness TickAgeMax=0.xx Period=0.35 Verdict=PASS`
- The SoakRout FINAL line with sane counters over the soak window; the flags-off byte-identity (the unchanged baseline FINAL line)
- The spawn baseline note (moved or not, with the number)

## Rollback

All gates are test/baseline-side; the soak variant is flag-gated. No sim-behavior rollback surface beyond `Battle.Morale.SoakRout=0`.

## Status (2026-10-31, the PR-chain session: PROBE + SOAK LANDED; the two-run extension waits on Task 1)

- The T4.8 staleness probe landed (`Battle.Morale.StalenessProbe`, the
  T2.9/T3.6 loaded shape with the morale pass on the SHIPPING natural
  period; the `MoraleStaleness` verdict line dumps at tick 2100 via the
  debug probe). The T4.8 gate-test hookup + one staged run remain.
- The SoakRout variant landed: the soak mix gains the general + the
  impetuous row; the morale session flags ride the mix; the FINAL line
  appends `routs/rallies/shattered/officerDeaths/moraleTicks` (the F14
  rule; flag off = zeros). The SoakRout staged nightly rides Task 2's
  unblock.
- The T4.7 two-run extension is NOT landed: its base gate
  (`DeterminismTwoRun`) is the unresolved Task 1 blocker -- a red-on-red
  extension has no isolatable morale signal. The writeup's disposition row
  anticipated exactly this; the extension lands with Task 1's closure.
- No-regression evidence: build green; unit 83/83; func 23/27 = exactly
  the four known failures (the Task 1/2 dossiers).
