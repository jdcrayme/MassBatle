# PR-9: Ranged perf gate (T3.6) + nightly soak variant + baseline notes

| | |
|---|---|
| **Finds** | Roadmap T3.6: "1,000 in flight < 0.3 ms sim; pool never allocates mid-volley"; the B.6/B.7 regression ladder needs the Phase 3 entry; `entity_spawn_80_us` needs the re-baseline with the ranged fragment present |
| **Blocks on** | The reference machine (nightly hardware path, R10) + one staged nightly run; baseline promotion is owner-gated per the audit H2 discipline |
| **Owner** | Baseline JSON acceptance (the candidate promotes only on a green compare, exactly like the T2.9 precedent) |

## Motivation

The T2.9 pattern is the template: the perf gate reads the measured `BattleMetrics` line (not the baseline JSON) from a dedicated fixture, and the nightly soak variant rides the existing `BattleMovementSoakController` behind a default-off flag whose zero state is byte-identical to the baseline. T3.6 needs its fixture (`Battle.Ranged.PerfStress`, shipped with PR-6), the soak variant wiring, the FINAL-line extension (positionally APPENDED per the F14 note -- `write_baseline.py` regexes are positional), and the re-baselined spawn metric.

## Proposed change

1. Soak variant: `Battle.Ranged.SoakVolley=1` on the existing controller (the `SoakClash` precedent): ~25% of spawned units become archer rows issuing fire orders on the order cadence; FINAL line appends `volleys=%d inFlightPeak=%d friendlyHits=%d poolDrops=%d`. Flag off = byte-identical T1.8/T2 baseline (R-C9).
2. Perf evidence: the `BattleMetrics` line extended with `StepMs/FireMs/InFlightPeak/PoolDrops` (PR-6's metrics); the gate asserts step + fire + hash-terms < 0.3 ms at the 1,000-in-flight peak; the measured line is recorded verbatim in the baseline notes (the 0.452 ms T2.9 precedent).
3. `entity_spawn_80_us` re-baseline with the ranged fragment + stat_sec fields present (the garrison precedent: 4.8 us -> measure, disclose the delta).
4. One staged nightly as evidence; the soak variant rides the next scheduled run (the engagement-phase precedent for deferring the staged run).

## Files

- Edit: `Plugins/MassBattle/Source/MassBattleTest/Private/BattleMovementSoakController.cpp` (variant + counters)
- Edit: `Tools/CI/write_baseline.py` (parse the appended counters into candidate metrics; reference JSONs keep carrying them forward via `--seed-from`)
- Edit: `Baselines/perf/ref-i7-9700F-1660Ti/nightly-smoke.json` (the spawn re-baseline, owner-gated)

## Evidence to gather on the reference machine

- `Battle.Ranged.PerfStress` at the 1,000-in-flight peak: `StepMs + FireMs + hash terms < 0.3 ms`, verbatim line
- Soak variant FINAL: `settled stuck=0 separation=0 archetypes=8 (spawn 8)` + sane volley counters; flag-off run byte-identical to the current baseline
- `compare_baseline.py` green on the promoted candidate (smoke duration + spawn us within tolerance)

## Rollback

The variant is default-off and additive; the baseline JSON only promotes on a green compare. Reverting the controller edit restores the exact current FINAL line.
