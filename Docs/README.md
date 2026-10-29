# MassBattle — Documentation Index

**Project:** UE5 Mass Entity + GAS battle simulation (Medieval 2: Total War clone). Solo dev + autonomous coding agents, brief-per-phase workflow.

**Engine pin:** UE 5.8.4 (ADR-13) · **All game C++:** `Plugins/MassBattle/Source/MassBattle/` (game module `MassBattleGame` stays empty)

> **New here?** Read `Docs/AGENTS.md` (standing protocol), then the status block below, then the read order for your task.

---

## Current status (2026-10-26)

| Item | State |
|------|-------|
| Pre-phase-0 (environment scaffold) | **COMPLETE** 2026-10-23 — plugin skeleton, maps, EDU fixture, smoke test green |
| Phase 0 (foundation: CI R8, registry, EDU parse, spawn, sync, death path, T0.1–T0.9) | **COMPLETE** 2026-10-24 — all tasks 0–13, DoD checked |
| Phase 0 audit + fix pass | **COMPLETE** 2026-10-24 — H1–H3 + M1–M8 + most lows fixed; evidence in `Docs/phases/phase-0/audit.md` §7 |
| Phase 1 (movement core: planner T1.9, layout T1.2, locomotion T1.3, compression T1.4, assigner T1.1, servo A1, OBB contact, unstuck v1, A2 orders, S1–S3, T1.8 soak) | **COMPLETE** 2026-10-25 — unit tier 44/44, func tier 7/7, nightly soak `stuck=0 separation=0 archetypes=6 ok=1`; report at `Docs/phases/phase-1/report.md` |
| Phase 1 extension — garrison (booking ledger, gray-box building/wall/trench fixtures, ingress/egress FSM, slot-source + Z servo, S8/S9/S10/S8b, TG.1–TG.9; executed between Phase 1 and Phase 2, rolled into `phase-1/` 2026-10-26) | **COMPLETE** 2026-10-26 — unit tier 50/50, func tier 14/14, nightly baseline soak `ok=1`, garrison soak variant evidence in `Docs/phases/phase-1/report-garrison.md` |
| Phase 1 audit + fix pass | **COMPLETE** 2026-10-27 — H1–H5 + M1–M19 + L-tier per `Docs/phases/phase-1/audit.md`; final evidence: unit 51/51, func 14/14, baseline soak + 1000-entity scale smoke `ok=1`; repair record in `Docs/phases/phase-1/report.md` §7 errata (residuals: M13 engine-verification, L8/L11 to Phase 2 planner work, L15/L16/L18 owner calls) |
| Phase 2 — formation movement cleanup (TW progressive fall-in & march discipline; owner plan 2026-10-27, brief at `Docs/phases/phase-2/brief.md`) | **COMPLETE** 2026-10-27 — Phases A-D landed behind default-off `Battle.Movement.*` cvars (ADR-19); unit 56/56, func 15/15 flag-off; report at `Docs/phases/phase-2/report.md`; D3 cvar fold-in deferred (checklist in report §6) |
| CI state | Per-commit gate + Jenkins `massbattle-gate` green; nightly runs smoke + T1.8 movement soak on the staged build (garrison-phase run green; baseline committed) |
| Phase 2 — engagement solver & melee (the combat heart; brief at `Docs/phases/phase-2/brief-engagement.md`) | **COMPLETE** 2026-10-27 — unit 66/66, func 21/21; T2.1–T2.9 green (pairing 0.452 ms < 0.5 ms @ 1k); ADR-24 StateTree REJECTED; report at `Docs/phases/phase-2/report-engagement.md` |
| CI state | Per-commit gate + Jenkins `massbattle-gate` green; nightly runs smoke + T1.8 movement soak on the staged build (garrison-phase run green; baseline committed; the engagement soak variant rides the next staged run) |
| **Next** | Phase 3 -- ranged stack: **Tasks 0-8 executed 2026-10-28/29** (build + unit tier 79/79 incl. nine `Battle.Unit.Ranged.*` suites; T3.6 perf gate PASS -- 0.236 ms @ 1,091 in flight < 0.3 ms; S6_Volley + SkirmishBehavior in the PR gate; save v2; soak variant; the B.7 entry). TWO disclosed blockers: the mixed clash+volley two-run equality (mm-scale servo transient, dossier in `Tools/CI/det_*.log`) and the pre-existing movement-soak dress-gate failure on the staged path (attributed via stash run; phase-2 territory). Report at `Docs/phases/phase-3/report.md`; progress at `Docs/phases/phase-3/progress.md`. Also pending: the ADR-27..31 owner applications + the ADR-28 arch-row amendment, the D3 movement fold-in checklist (`Docs/phases/phase-2/report.md` §6), and the S4_ShieldWall/S5_Mob melt decision (phase-2 dossier). |

**Deferred items carried into Phase 2** (from A4 dispositions + report §4): shipping split of test deps — **DONE in Phase 1** (`MassBattleTest` DeveloperTool module) · `.uproject` `TargetAllowList` pruning of MCP toolset plugins (owner call, still deferred) · EDU floats >2 decimals remain rounded (documented rule) · soak-map cook-list entry — **DONE in Phase 1** (`MapsToCook` + `Content/Data` staging) · re-baseline `entity_spawn_80_us` on the nightly hardware path (T2.9).

---

## Doc map

| Path | What it is | Status |
|------|-----------|--------|
| `Docs/AGENTS.md` | Standing agent protocol — prime directives, run protocol, errata flow, report standard, doc conventions, brief template | **Living** |
| `Docs/README.md` | This file — status, map, read orders | **Living** (update with every phase) |
| `Docs/spec/architecture.md` | System architecture + ADR decision log (ADR-01…17) | **Living spec** |
| `Docs/spec/garrison-movement.md` | Garrison movement design spec: B1-B7 behavior contract, data model, mechanics §4, ADR-18…21 proposals | **Living spec** (owner-authored; implementing agents read cited sections only) |
| `Docs/spec/engagement-and-melee.md` | Engagement solver & melee design spec: §0 EC-table acceptance contract, §3 data model, §4 mechanics, §6 touch points, ADR-22…26 proposals | **Living spec** (owner-authored; implementing agents read cited sections only) |
| `Docs/spec/ranged-stack.md` | Ranged stack design spec (Phase 3): §0 RC-table acceptance contract, §3 data model (fragment + pool), §4 mechanics (ballistics, LOS, volleys, impacts), §6 touch points, ADR-27…31 proposals | **Living spec** (owner-authored; implementing agents read cited sections only) |
| `Docs/spec/roadmap-and-testing.md` | Build order (Phase 0–6), test gates T0–T6, scenarios S1–S7, automation strategy | **Living spec** |
| `Docs/spec/reference-machine.md` | Perf-baseline hardware record (R10) | **Living** |
| `Docs/reference/engine-api-5.8.md` | Pinned UE 5.8.4 API reference: Part A audit (C1–C12 verdicts), Part B operational digest (§4.0–§4.13), Part C sources | **Living reference** |
| `Docs/reference/mcp-playbook.md` | Epic Unreal MCP operating manual — session lifecycle, call shape, toolsets, quirks ledger | **Living reference** |
| `Docs/reference/environment.md` | Install/project/VCS/config facts + facts-correction ledger | **Living reference** |
| `Docs/phases/pre-phase-0/` | brief · progress · report (environment scaffold run) | **Archive** |
| `Docs/phases/phase-0/` | brief · progress · report · audit (foundation run + post-run audit) | **Archive** (report §7 = Phase 1 entry point) |
| `Docs/phases/phase-1/` | brief · progress · report · audit + the rolled-in garrison run (`brief-garrison` · `progress-garrison` · `report-garrison`) | **Archive** (report §4 + report-garrison §4 = Phase 2 entry points) |
| `Docs/phases/phase-2/` | Movement-cleanup run: brief · progress · report (**Archive**). Engagement-solver run: `brief-engagement` · `progress-engagement` · `report-engagement` (**Archive**). Post-run audit: `audit-engagement` (2026-10-28) + first-pass verification record: `audit-engagement-verification` (2026-10-28) | |
| `Docs/phases/phase-3/` | Ranged-stack run: brief · kickoff-verification (fixes F-K1..K8) · progress (Tasks 0-8) · report (2026-10-29) | **Archive** (the phase-4 entry point is the report's section 5) |
| `Docs/reviews/plan-review-findings.md` | 2026-10-23 plan review: errors E1–E6, risks R1–R13, improvements I1–I7, evidence + citations | **Archive** (referenced by live specs) |

Repo files outside `Docs/` you will be pointed at: `Tools/CI/README.md`, `Tools/CI/JENKINS.md`, `Tools/CI/run_tests.bat` / `nightly.bat` / `compare_baseline.py`, `Baselines/perf/README.md`, `Jenkinsfile`.

---

## Read orders by task

**Starting Phase 1 (implementing agent):**
1. `Docs/AGENTS.md` → this file
2. `Docs/spec/roadmap-and-testing.md` — Part A "Phase 1 — Movement core" + T1 gate table + B.0/B.8
3. `Docs/spec/architecture.md` — §8 (movement), §6–7 as needed, §12 budgets
4. `Docs/phases/phase-0/report.md` §7 ("Where Phase 1 should start") — ready hooks, harness shape
5. `Docs/phases/phase-0/audit.md` — §2 contract-to-test drift pattern: the failure mode to not repeat; §5 fix order as method
6. `Docs/reviews/plan-review-findings.md` — R2 (Mass Avoidance), R4 (path planner)
7. `Docs/reference/engine-api-5.8.md` — cite, don't re-derive

**Writing the Phase 1 brief (owner/preparer):**
`Docs/AGENTS.md` §8 (template) → `Docs/phases/phase-0/brief.md` (as the worked example) → deferred-items list above → roadmap Phase 1 scope.

**Verifying an engine API symbol:** `Docs/reference/engine-api-5.8.md` first; only open pinned engine source for facts not recorded there, then record what you find (AGENTS §4).

**MCP / editor automation work:** `Docs/reference/mcp-playbook.md` — check the §8 correction ledger before rediscovering anything.

**CI, baselines, perf gates:** `Docs/spec/roadmap-and-testing.md` Part B → `Tools/CI/JENKINS.md` + `Baselines/perf/README.md` → `Docs/phases/phase-0/report.md` §1 (what was built) + §3 (ADR-16 deviations).

**"Why is it built this way?"** `Docs/spec/architecture.md` §0 ADR log → `Docs/reviews/plan-review-findings.md` (evidence per E/R/I id) → `Docs/phases/phase-0/audit.md` §4 (verified-correct items — do not re-litigate).

**Debugging a "fact fight" (docs vs reality):** `Docs/AGENTS.md` §2.1 + §4 — reality wins; correct the owning reference file, log it. Historical corrections: `Docs/reference/environment.md` §8 ledger.

---

## Conventions

- Cross-references are repo-root-relative (`Docs/…`). Shorthand used by historical docs (arch / roadmap / findings / BRIEF / §4.x) maps via `Docs/AGENTS.md` §7.
- One fact, one home: engine API → `reference/engine-api-5.8.md` · MCP → `reference/mcp-playbook.md` · environment → `reference/environment.md` · hardware → `spec/reference-machine.md`. Everything else links.
- Phase lifecycle: `phases/phase-N/{brief,progress,report,audit}.md` — briefs are frozen at kickoff; corrections land in reference files, never retroactively in completed briefs.
- Reorganized 2026-10-25 from the original flat layout (`tw-battle-system-*.md`, `phase0-*.md`, `Docs/agent/*`); every internal cross-reference was rewritten. Archive notes in each moved file record the old paths.
