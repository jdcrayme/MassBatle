# Patch series (Patches/)

Two series live here:

## Phase-2 engagement audit (2026-10-28)

Branch: `audit/phase2-engagement` (from `main` @ `5e46a1b`).
Findings register: `Docs/phases/phase-2/audit-engagement.md`.

Each `.patch` file is one commit; apply in order with `git am`:

| # | Patch | Groups | Must re-run on the UE machine |
|---|-------|--------|-------------------------------|
| 1 | `0001-...scaffolding.patch` | C-1 C-2 C-4 C-5 | full unit + func tier (deletions only) |
| 2 | `0002-...cull-static-maps.patch` | A-2 | func tier (soak/escalation metrics unchanged) |
| 3 | `0003-...body-radius-home.patch` | B-1 B-5 | T2.1 golden (formality) |
| 4 | `0004-...roll-hash-and-finddefender.patch` | B-2 A-4 | T2.1 golden + T2.3 + T2.8 DeterminismTwoRun |
| 5 | `0005-...t29-stub-and-guard.patch` | C-6 C-3 | unit tier (one test removed, expected) |
| 6 | `0006-...draincompositepool.patch` | A-3 | T2.1 golden (block removed, expected green) |
| 7 | `0007-...fixture-data.patch` | B-3 D-3 B-7 B-8 B-6 | S5_Mob + CombatSmoke (fixture path touched) |
| 8 | `0008-...doc-map.patch` | docs | none |

PR writeups from that audit: `PRs/PR-1` .. `PR-5` (below).

### Landing record (2026-10-29): the PR sweep

The five audit PRs + PR-8's visual remainder were executed on the
machine (commits `45faecd`, `074ce8f`, `7c48d89`, `46d0c1c`, `3b3a528`,
`e085dbb` + the wrap-up): PR-3 (tile extent one home), PR-2 (asset-first
input + retained fallback), PR-1 (scenario assets + the SpawnClash asset
channel), PR-8 (gray-box arrow feed + SM_Arrow/M_ArrowGray), PR-4
(registry homes + intent seam + the knockback as a seam function -- the
writeup's dedicated processor was A/B-falsified, see the PR-4 status
section). PR-5 was already executed as `2230eb5`; PR-6/7/9 were executed
by the phase-3 run (phase-3 report section 6). Per-writeup status
sections are in each `PRs/PR-*.md`. Tiers at wrap-up: unit 80/80, func
19/23 with exactly the four known pre-existing failures. Editor-scoped
residuals (owner actions): the live keybind-retune + keypress pass
(PR-2), the PIE corner-click eyeball (PR-3), the per-map scenario-asset
assignment via the editor UI (PR-1), the arrow-mass screenshot (PR-8).

## Phase-3 kickoff (2026-10-29)

Branch: `phase3/kickoff` (from `main` @ `48f1553`).
Companion docs: `Docs/spec/ranged-stack.md` (the design spec) + `Docs/phases/phase-3/brief.md` (the phase brief; its task plan consumes this series).

The brief-per-phase workflow, extended to the kickoff itself: everything a
STATIC environment can land before the UE machine touches the phase -- the
spec + brief, the EDU `stat_sec` extension + ranged fixture rows + spawn
mapping, the gate skeletons (fragments + CVAR block + failing T3.4/T3.5
stubs), both pure-math seams fully implemented and unit-locked (ballistics,
volley/ammo/skirmish, the projectile pool), and the state-hash ranged terms
+ pool block. The implementing agent's Task 0 applies + verifies this series
(`Docs/phases/phase-3/brief.md` §5) before building the pipeline.

| # | Patch | Contents | Must re-run on the UE machine |
|---|-------|----------|-------------------------------|
| 10 | `0010-...docs.patch` | the ranged-stack spec + the phase-3 brief + the README map/status | none (docs only) |
| 11 | `0011-...gate-skeleton.patch` | `FRangedStateFragment`, `Battle.Ranged.*` CVAR block, `ECombatHashFlags`, FUnitRuntimeState toggle fields, S6/skirmish fail-by-design stubs | unit tier green; func tier red on exactly the two skeletons (counted) |
| 12 | `0012-...edu-data-spine.patch` | `stat_sec` schema (15 keys) + 2 ranged fixture rows + FromEdu missile branch + spawn stamping + T0.2 golden re-sign + SpawnComposition extension | unit tier (Battle.Data.* re-signed goldens, SpawnComposition, spawn-dependent suites) |
| 13 | `0013-...ballistics-seam.patch` | `BattleBallistics` (solver, dispersion, stepper, LOS march) + `SampleHeight` + T3.1/T3.2 | unit tier (the two new tests; FMA watch item documented) |
| 14 | `0014-...volley-pool-seams.patch` | `BattleProjectilePool` SoA + `SelectVolleyShots`/`EvaluateSkirmish` bodies + T3.3 family (Pool/AmmoAccounting/VolleySelection/SkirmishMath) | unit tier (six Battle.Unit.Ranged.* green; cvar-default dependence documented) |
| 15 | `0015-...state-hash-ranged-terms.patch` | hash blocks 8 (ranged terms) + 9 (pool, index order) + `UBattleProjectileSubsystem` + HashTerms extension | unit tier (HashTerms + ranged suite; melee-only hashes unchanged by construction) |
| 16 | `0016-...pr-writeups.patch` | this README section + `PRs/PR-6` .. `PR-9` | none (docs only) |

PR writeups (require UE / MCP / owner decisions -- NOT in the patch series):

- `PRs/PR-6-ranged-fire-and-step-processors.md` (the pipeline: fire + step processors, impact resolution, registry surface, ExecBridge fixtures; full tier cycle)
- `PRs/PR-7-s6-and-skirmish-scenarios.md` (the T3.4/T3.5 scenario bodies; blocks on PR-6)
- `PRs/PR-8-m4-arrow-demo-visualization.md` (arrow ISM asset + PIE; gray-box now, Niagara volley batching in Phase 5)
- `PRs/PR-9-ranged-perf-gate-and-soak-baselines.md` (T3.6 fixture evidence + soak variant + reference-machine baselines)

## Phase-4a kickoff (2026-10-31)

Branch: `phase4/kickoff` (from `main` @ `4357321`).
Companion docs: `Docs/spec/morale-and-gas.md` (the design spec) + `Docs/phases/phase-4/brief.md` (the 4a run brief; its task plan consumes this series).

Owner decisions baked into the series (2026-10-31): the E3 tick mechanism
is the periodic MoraleTick GE (ADR-33 proposal); the phase splits 4a (the
gated morale core, this series + PRs) / 4b (OrderLatency + TerrorAura +
climate fatigue, its own brief at its kickoff -- the phase-2 two-run
precedent). The brief's Tasks 1-2 are the inherited phase-3 blockers
(the mixed two-run mm-scale divergence; the staged soak dress gate) --
they precede all morale work in the run's task order.

The brief-per-phase workflow, kickoff extended exactly as Phase 3 did:
everything a STATIC environment can land before the UE machine touches
the phase -- the spec + brief, the EDU officer key + stat_mental
discipline/training consumption + the fixture re-sign, the gate
skeletons (four morale enums, the Battle.Morale.* CVAR block, the
registry morale block, bOfficer fragment value + the dead FOfficerTag
deleted, OfficerCount/bIsGeneral authoring, save v3, four fail-by-design
scenario stubs), the full BattleMorale seam unit-locked by T4.1/T4.2 +
the RoutRally suite, and the state spine (hash block 10 + the
SaveRoundTrip/HashTerms extensions). The implementing agent's Task 0
applies + verifies this series (`Docs/phases/phase-4/brief.md` section 5)
before the blocker hunts and the pipeline tasks.

| # | Patch | Contents | Must re-run on the UE machine |
|---|-------|----------|-------------------------------|
| 24 | `0024-...docs.patch` | the morale-and-gas spec + the phase-4a brief + the README map/status + the roadmap run-split annotation | none (docs only) |
| 25 | `0025-...gate-skeleton.patch` | the four morale enums + `FUnitRuntimeState` morale block + `FUnitSpawnSpec` OfficerCount/bIsGeneral, the `Battle.Morale.*` CVAR block (20 flags, Enable default 0), `FBaseStatsFragment.bOfficer` + `FOfficerTag` deleted, the seam surface with task-0 stubs, the four fail-by-design scenario stubs (S7_RoutCascade / OfficerDeath / EffectEndToEnd / ToggleBehaviors) | unit tier green; func tier red on exactly the four skeletons ON TOP of the four known (counted) |
| 26 | `0026-...edu-data-spine.patch` | the `officer` EDU key (optional + repeatable <= 3, the per-key occurrence cap -- rule 7's one sanctioned exception, disclosed; SchemaFieldCount 15 -> 16), the fixture officer lines (0/2/1/1), FromEdu discipline/training loud-map + officer count, the spawn path (officers as extra roster entities, bOfficer stamping, the morale spawn stamp), the registry morale APIs + per-team general records, the two new native tags, the death-path officer branch (gated on Enable) | unit tier (Battle.Data.Edu* re-signed; spawn-dependent suites -- roster sizes grow by the officer counts; SpawnComposition count UNCHANGED); func tier (no new failures) |
| 27 | `0027-...morale-seam.patch` | the BattleMorale seam bodies (the ten-term weighted stack, the closed-form half-life decay, the exertion integral + six-band ladder + the golden multiplier table, the rout FSM with the additive discipline offsets, the rally chance + decision) + the T4.1/T4.2/RoutRally unit suites; the discipline-break-scaling correction (additive offsets -- the multiplicative form is degenerate at the 0-anchored default threshold, disclosed) | unit tier (the three Battle.Unit.Morale.* suites green) |
| 28 | `0028-...state-spine.patch` | save v3 (the morale block; restore accepts 2/3, v1 aged out -- one legacy back, the v2 precedent) + hash block 10 (unit-level morale terms, after the pool block) + the SaveRoundTrip/HashTerms extensions | unit tier (SaveRoundTrip + HashTerms re-signed green) |
| 29 | `0029-...pr-writeups.patch` | this README section + `PRs/PR-12` .. `PR-15` | none (docs only) |

PR writeups (require UE / MCP / owner decisions -- NOT in the patch series):

- `PRs/PR-12-morale-pipeline.md` (the MoraleTick GE + exec calc + fatigue catalog + MoraleTickProcessor + the UnitSync corrections + the D-4 mirror move + the ExecBridge surface; blocks on Task 3's GAS API audit)
- `PRs/PR-13-morale-scenarios.md` (the four T4.3-T4.6 scenario bodies; blocks on PR-12)
- `PRs/PR-14-m5-demo-graybox.md` (the demo-map M5 sample + the PIE eyeball; the PR-8/PR-11 posture)
- `PRs/PR-15-determinism-staleness-soak.md` (the T4.7 two-run extension + the T4.8 staleness fixture + SoakRout + the baselines; blocks on PR-12/13 + Task 1's disposition)

## Verification status

Static authorship for the phase-4a series (same posture as the phase-3
kickoff): every patch is brace-balanced, include-complete, and every new
constant is an A16-disclosed placeholder (ADR-37). No UE build was
available at author time; every commit message carries its MUST-re-run
note per AGENTS section 2 (evidence before claim). The golden re-signs
(schema 16-key + officer lines; save v3; the discipline-offset
correction) are disclosed in their owning patches. Byte-exact splicing
used for every existing-file edit (the F-K tab-expansion lesson,
re-verified this session); every patch applies cleanly, in order, on a
pristine `main` tree.

Watch item inherited with the series: hash block 10 appends REAL
default terms per unit, which changes every existing full-hash VALUE
(the two-run equality class is unaffected -- both runs hash the same;
no stored hash literals exist; disclosed in 0028's commit note).

## Verification status

Static authorship for the phase-3 series (same posture as the audit series):
all patches are brace-balanced, include-complete, and every new constant is
an A6-disclosed placeholder (ADR-30). No UE build was available at author
time; every commit message carries its "MUST re-run" note per AGENTS §2
(evidence before claim). The golden re-signs (T0.2 schema, SpawnComposition
growth) are disclosed in `0012` per the standing re-sign policy.

First-pass verification (2026-10-28, no UE machine): the series applies
cleanly via `git am` and was CORRECTED in-branch before landing on main --
one UHT compile blocker (`EBattleProjectileKind` needed `UENUM()`, `0011`),
one fatal pool-test index (`0014`), doc nits (`0016`), and the authoring
tool's whole-series tab->space expansion restored (raw diff +8,168/-5,259 ->
+2,970/-49). The `.patch` files here are the corrected-series exports.
Machine verification the same day (Task 0 of the implementing run): build
green, unit tier 75/75 -- two more defect classes surfaced and were fixed on
main (F-K7 compile, F-K8 runtime test defects). Full record:
`Docs/phases/phase-3/kickoff-verification.md` (fixes F-K1..K8) +
`Docs/phases/phase-3/progress.md`.

## Phase-3 cumulative audit (2026-10-30)

Branch: `audit/phase3-redo` (from `main` @ `86d1a1f`).
Findings register: `Docs/phases/phase-3/audit.md`.

Scope: the phase-3 ranged-stack run, the PR-sweep landing, and every
ranged-facing surface they touched. The pattern finding: phase 3 re-scattered
each constant family the phase-2 audit had unified (body radius, hash-family
multipliers, EDU row indices in fixtures) -- plus the demo's faction split
keyed on EDU row order. All unified again, this time into homes in headers
every seam already includes. Value-neutral by construction; identical bits.

| # | Patch | Groups | Must re-run on the UE machine |
|---|-------|--------|-------------------------------|
| 17 | `0017-...body-geometry.patch` | B-1 | unit tier (T3.1-T3.3 goldens use 1.6f; zero drift expected) |
| 18 | `0018-...hash-family.patch` | B-2 | unit tier (T2.1 + T3.1 goldens; DeterminismTwoRun unchanged) |
| 19 | `0019-...contact-angle.patch` | B-6 | unit + func tier (impact sectors + ring points) |
| 20 | `0020-...fixture-honesty.patch` | B-3, B-4, A-1 | full unit + func tier; SpawnVolley/PerfStress; one SoakVolley nightly; PIE (adopt-path tinting) |
| 21 | `0021-...fire-pass.patch` | C-1, C-6, C-7, B-8 | full unit + func tier (same EXEC_RESULT lines expected) |
| 22 | `0022-...test-cvar-list.patch` | C-3 | func tier (S6/Skirmish green; DeterminismTwoRun keeps its blocker) |
| 23 | `0023-...docs.patch` | docs | none (docs only) |

PR writeups (editor/machine-gated; NOT in the patch series):

- `PRs/PR-10-ranged-config-assets.md` (BuildT0Configs authors 4 assets, the repo holds 2)
- `PRs/PR-11-level-authoring-row-references.md` (dictionary-keyed level authoring + the demo-map TeamId eyeball)

Authoring note, recorded for the next session: the editing surface in this
environment expands tabs to spaces on write (the kickoff's F-K class, again).
The audit's first three commits were caught by numstat review and redone via
scripted byte-exact splicing; every exported patch above was verified to
apply cleanly, in order, on a pristine `main` tree.

### Landing record (2026-10-30): the audit series lands on main

The full series (patches 17-23, seven commits) was applied to `main`
directly with `git am`, authorship and messages preserved:
`d4aa382` (17) `3a8c992` (18) `742948b` (19) `0653993` (20) `a9b6f06`
(21) `1098e0d` (22) `72db428` (23). Verification evidence:

- Byte-exactness: every landed commit's numstat matches its patch's
  declared diffstat. The one cumulative delta (RangedFireProcessor.cpp
  shows 41 changed lines, not 17+21's 42) is the expected overlap: both
  patches rewrite the same blocker-query line.
- Stale-symbol scan clean post-landing: no `VolleyBodyRadiusM`, no
  `KnuthGold`/`KnuthMix`, no raw 2654435761u/40503u/2685821657u outside
  BattleTypes.h, no `RangedNames[4]` locals in the test actor, no
  `EduRowIndex == 0` faction keying in the demo.
- Build: `Build.bat MassBattleEditor Win64 Development` green after the
  landing (all three modules; re-verified after touching every changed
  .cpp). The only warning in the tree is the pre-existing
  `BattleSpawnLibrary.cpp(358)` C4457 -- a file no patch touches.
- Still owed on the machine (each commit's MUST-re-run note): unit +
  func tiers, SpawnVolley/RangedPerfStress, one SoakVolley nightly, and
  the PIE adopt-tinting eyeball (patch 20's behavior-visible half).

PR-10 / PR-11 remain PENDING (editor + owner; no Unreal MCP session was
available at landing time). PR-10's static check ran clean and supports
the writeup's recommended Option B: no code consumes the two ranged-row
assets (`Battle.Data.EntityConfigIntegrity` iterates only
`PeasantLevy`/`SergeantSpearmen`; nothing else references the names) --
but dropping the authoring rows is the owner's call. Per-writeup notes
added to each `PRs/PR-*.md` status section.

### Machine verification + the PRs execute (2026-10-30, editor session)

One editor session (MCP over the HTTP server) closed every item the
landing left open except the staged nightly. Evidence, per item:

- **Unit tier: 80/80 Success** (in-editor `AutomationTestToolset`,
  filter `Battle.Unit+Battle.Data+Battle.Smoke`) -- includes the nine
  `Battle.Unit.Ranged.*` suites, `Combat.HashTerms` (patch 18's golden),
  the EDU/SpawnComposition goldens (patch 20), and
  `EntityConfigIntegrity`. Patches 17-22's unit-half MUST-re-run notes
  are thereby cleared.
- **Func tier: passed=19 failed=4 total=23** (`run_tests.bat func`,
  headless standalone) -- exactly the four known pre-existing failures
  (DeterminismTwoRun / S4_ShieldWall / S4_WidthOverride / S5_Mob). No
  new failures.
- **PIE (demo map, patch 20's behavior half + PR-11)**: the five
  re-authored level units spawn `DemoSpawn spawned=5 of 5` through the
  new dictionary path with `VisualRegister` Team=0 x3 / Team=1 x2; the
  loud-fail negative test (`Not_A_Row` -> `DemoSpawn FAIL ...
  absent from the EDU fixture`, `spawned=4 of 5`) held.
- **SpawnVolley skirmish boot** (`Tools/CI/audit_volley.log`): the
  adopt path registered the exec-spawned ARCHERS (EDU row 2) as Team=0
  and the levy target as Team=1 -- under the pre-audit
  `EduRowIndex == 0` keying the archers would have tinted Enemy;
  `SkirmishRetreat Unit=1:1 ... FireTarget=2:1` (the phase-3 line
  shape) and 490 `RangedFire` lines.
- **PR-10 executed as Option B** (owner decision): `94d47e3` restores
  the two-row table. **PR-11 executed**: `ff542af` (property + map).
  Status sections in each writeup carry the full evidence.

**Perf-gate watch item (honest record):** `Battle.Ranged.PerfStress`
returned `Verdict=OVER` twice this session (RangedMs 0.335 / 0.325 vs
the 0.300 budget; the phase-3 reference was 0.236 PASS). The elevation
is NOT attributed to the audit series: `StepMs` -- untouched code with
identical-value constants -- rose by the same proportion (0.063 ->
0.090), and `InFlightPeak` varies run-to-run (1045/1048 here vs 1091 in
the phase-3 capture), consistent with the known D-3 two-run divergence
blocker and a machine-state-sensitive wall-clock gate. Re-baseline on
the quiet nightly path before drawing a conclusion. `PoolDrops=0` in
both runs. `Tools/CI/audit_perf.log` holds the second sample (the
first was overwritten in place and survives only in the numbers quoted
here). The SoakVolley=1 nightly stays with the staged CI schedule (not
re-run here -- hours-long).

## Melt diagnosis (2026-10-30, ad-hoc failing-test session)

Diagnosed and LANDED the same day: the S4_ShieldWall/S5_Mob "melt" failures
were a write-back indexing bug, not the defensive-intent design question.
Full evidence chain in the `Docs/phases/phase-2/audit-engagement-verification.md`
CORRECTION section; probe logs `Tools/CI/probe_s4d.log` (the misalign),
`probe_full.log` (the half-fix's dropped rows), `fix2_s4.log` (the melt
registering post-fix).

| Commit | Contents |
|---|---|
| `21d73d1` | the write-back's Seekers offset corrected (chunk's seeker count, not per chunk) |
| `2c81bc0` | the complete fix: TWO cursors -- `ChunkCursor` (per-chunk, into `ChunkIndices`) and `SeekerCursor` (per-seeker, into `Seekers`); the shipped code used one cursor for both, so every chunk after the first either cross-contaminated units (friendly-fire kills) or lost its rows entirely |

Post-fix tiers: unit 80/80; func 19/23 with PairLifecycle green and exactly
the four known failures -- but the melts' SIGNATURES are now honest:
S4_ShieldWall registers the EC2 melt curve and fails only on wallHeld
sample count (2 < 3 -- the wall collapses inside the 15-tick sample
cadence; `FBaseStatsFragment` Lethality/Shield still ride spawn defaults
1.0 instead of the EDU 0.35/0.2, the phase-3 report's disclosed T6.1 gap,
roughly doubling kill rates); S5_Mob pairs (exposed=4 waiters=58) but the
pull-down still misses the band. The remaining melt work is test
recalibration + the T6.1 stamping decision, not machinery repair.
DeterminismTwoRun (two-run divergence) and S4_WidthOverride (0.285 vs
0.300 separation bound) are unchanged, each with its documented owner
next step.
