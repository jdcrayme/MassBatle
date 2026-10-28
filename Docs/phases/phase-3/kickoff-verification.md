# Phase-3 kickoff -- first-pass verification record (2026-10-28)

Static verification of the phase-3 kickoff bundle (transport directory `TMP/`:
patches `0010..0016` + the spec/brief/PR markdown) against `main` @ `48f1553`,
per the phase-2 audit's precedent (`Docs/phases/phase-2/audit-engagement-verification.md`).
No UE build was available in this environment; everything the machine must
confirm is listed under "Deferred" and is exactly the implementing run's Task 0.

## What happened

1. The series applies cleanly: `git am TMP/0010..0016` on `phase3/kickoff`
   (from `main` @ `48f1553`) -- zero conflicts.
2. Static review found one UHT compile blocker and one fatal-at-runtime test
   bug, plus doc nits and a whole-series whitespace defect (authoring-tool
   tab expansion). All fixable items were fixed IN the series (rebuilt on
   `phase3/kickoff`, fix set verified: whitespace-normalized diff of the
   rebuilt series vs the originals differs ONLY by the fixes below), then
   fast-forwarded to `main`. The corrected series is exported as
   `Patches/0010..0016.patch` (supersedes the TMP originals; same commit
   subjects/messages, so the Patches/README table's filenames hold).

## Fixes folded into the series

| ID | Commit | Defect | Fix |
|----|--------|--------|-----|
| F-K1 | `0011` | UHT blocker: `EBattleProjectileKind` is a plain `enum class` used as a UPROPERTY type in `FRangedStateFragment::Kind` and `UBattleUnitTrait::Kind` -- unreflected enums cannot be UPROPERTY types | `UENUM(BlueprintType)` on the enum in `BattleTypes.h` (house precedent: `ECombatPairState`; `BattleTypes.h` already includes its `.generated.h`) |
| F-K2 | `0014` | Fatal in dev builds: `FRanged_Pool`'s churn rounds run against a FULL pool (the fill/release/re-acquire above leaves the free list empty), so `Acquire` fails closed into `InvalidIndex` and the `PosX[S]` write hits TArray RangeCheck's `checkf` (hard crash, not a test failure) | drain the pool to zero before the burst rounds (+ a `drain returns to zero live` assert); all churn assertions still hold |
| F-K3 | `0013`/`0014` | Stale file-header comment in `MassBattleRangedTests.cpp` ("T3.3 family lands with the seam task") -- true at `0013`, false from `0014` on | "unit-locked below" |
| F-K4 | `0016` | PR-6 says "the T3.9 fixture"; the roadmap gate table has no T3.9 (the perf gate is T3.6, which PR-9 itself cites) | T3.9 -> T3.6 |
| F-K5 | `0016` | PR-7/8/9 header tables malformed (first row `| |---|`) | house shape (`| | |` + separator), matching PR-1..6 |
| F-K6 | all | The authoring pipeline expanded tabs to 8-space equivalents in every C++ file it touched: 17 modified files became whole-file rewrites (raw series diff +8,168/-5,259 for a real change of ~+2,970/-49) and 5 of 10 new files shipped space-indented, split against 5 tab-indented ones | per-commit diff-driven reconstruction: parent-exact lines for unchanged regions, expansion-inverse (tab-stop-8) re-tabbing for the changed lines, with the byte-exact invariant `expand8(result) == expand8(blob)` asserted per file; main's intentional mixed tab+space alignment lines are preserved untouched |

## Static verification performed (all green)

- Includes complete, braces balanced on all 10 new C++ files; every new symbol
  (`FRangedStateFragment`, `EBattleProjectileKind`, `ECombatHashFlags`,
  `FRangedCVars`, pool/subsystem API, seam functions) is defined exactly once
  and consumed consistently.
- EDU spine: schema 14 -> 15 keys with `stat_sec` (same arity table as
  `stat_pri`); disk golden == the test's embedded canonical byte-for-byte
  (4 units, 2 ranged rows); `FromEdu`'s missile mapping (pri[2] arrow|bolt|no,
  range pri[3], ammo pri[4], melee from sec[0..1]) agrees with the
  `BuildT0Configs` constants on every stat; edge cases cover stat_sec arity +
  missing-key rejection.
- T3.1 goldens recomputed by hand (v=g=d=10 -> theta 45 deg, T = sqrt(2);
  out-of-envelope beyond v^2/g; dispersion cone bound holds via the
  atan-of-tangent-radius argument); T3.2 hill/bump/equal-height cases
  consistent with `MarchClear`'s strict-greater rule; `SampleHeight` bilinear
  corner case (4 m cell between flats -> 1.0 m) correct.
- State hash: ranged block (AmmoCount, ReloadTimer 1e-2, ShotsFired) appended
  after the charge block, zero-filled when absent; pool block (live count +
  Pos/Vel/TTL at 1e-2 in index order) appended last -- exactly spec
  ranged-stack section 3.4; accessor pattern identical to the existing
  charge/garrison blocks; `EmptyPool()` folds zero for the flags-off baseline.
- Test names match the brief's T3 gate map: `Battle.Unit.Ranged.{Ballistics,
  LOS, Pool, AmmoAccounting, VolleySelection, SkirmishMath}` + the two
  fail-by-design func skeletons `Battle.Func.{S6_Volley, SkirmishBehavior}`
  (registered via the factory registry, counted per Task 0).
- Archetype posture: arrow and bolt share ONE ranged composition (Kind is a
  fragment value), so F6's 6 -> 8 (alive/dead) is consistent; SpawnComposition
  measures + itemizes the growth (spec section 3.1's "re-signs to the measured
  count" wording) rather than hard-coding 8.
- Doc maps complete: `Docs/README.md` (spec, brief, phase-3 row, Next row) and
  `Patches/README.md` (phase-3 series table) cover every new artifact.

## Deviations to carry into the phase report (spec-vs-seam, disclosed)

- `SelectVolleyShots` returns the shot count (`int32`; spec section 4.4 says
  `void`) and takes `FriendlyImpactTolerance` as `int32` (spec: `float`) --
  the processor needs the count for metrics; the cvar is int32.
- `FVolleyShooterView` carries `RangeM` (spec's struct omits it; the
  `(MinRangeM, RangeM]` eligibility test needs it per-shooter).
- The flat-lane corridor uses half-width `2*BodyRadiusM + HitRadiusM` (a
  friendly body circle vs the (R+HitRadius) corridor); the spec's "width"
  wording is ambiguous -- code and tests are self-consistent.
- A6 placeholder constants (speeds 35/60, dispersion 4/2 deg, reload 3 s, et
  al.) are golden-locked pending the Phase-6 T6.1 port-or-deviation audit.

## Deferred to the UE machine (the implementing run's Task 0)

- Unit tier green including the re-signed goldens (`Battle.Data.Edu*` 15-key,
  `Battle.Unit.Ranged.*` x6, `SpawnComposition` growth) -- F-K1/F-K2 change
  compile/runtime behavior and MUST be build-verified.
- Func tier red on exactly the two skeletons (counted, listed in the progress
  log); no other func regressions.
- Archetype count 8 measured at spawn; `SampleHeight` bilinear evidence on a
  synthetic hill; any same-machine FMA golden re-signs (disclosed per AGENTS
  section 4); flags-off identity (unchanged T1.8/T2 soaks).

## Open blockers

None. PR-6..PR-9 remain the UE/MCP-gated remainder (their writeups in
`Patches/PRs/` are planning documents, execution pending the pipeline tasks).
