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
