# MassBattle — Documentation Index

**Project:** UE5 Mass Entity + GAS battle simulation (Medieval 2: Total War clone). Solo dev + autonomous coding agents, brief-per-phase workflow.

**Engine pin:** UE 5.8.4 (ADR-13) · **All game C++:** `Plugins/MassBattle/Source/MassBattle/` (game module `MassBattleGame` stays empty)

> **New here?** Read `Docs/AGENTS.md` (standing protocol), then the status block below, then the read order for your task.

---

## Current status (2026-10-25)

| Item | State |
|------|-------|
| Pre-phase-0 (environment scaffold) | **COMPLETE** 2026-10-23 — plugin skeleton, maps, EDU fixture, smoke test green |
| Phase 0 (foundation: CI R8, registry, EDU parse, spawn, sync, death path, T0.1–T0.9) | **COMPLETE** 2026-10-24 — all tasks 0–13, DoD checked |
| Phase 0 audit + fix pass | **COMPLETE** 2026-10-24 — H1–H3 + M1–M8 + most lows fixed; evidence in `Docs/phases/phase-0/audit.md` §7 |
| CI state | Per-commit gate + Jenkins `massbattle-gate` green (build 3); nightly wired (`massbattle-nightly`, cron 02:00 — **Jenkins must be running for cron to fire**, start line in `Tools/CI/JENKINS.md`) |
| **Next** | **Phase 1 — Movement core** (roadmap Part A Phase 1). Brief not yet written. |

**Deferred items carried into Phase 1** (from `Docs/phases/phase-0/audit.md` §7 + fix notes):
shipping split of `FunctionalTesting`/`Gauntlet` deps into a Developer-type test module · `.uproject` `TargetAllowList` pruning of MCP toolset plugins (owner call) · EDU floats >2 decimals remain rounded (documented rule) · `L_MassBattle_T0_Functional` needs a cook-list entry before a staged-build func tier · `MassEntityHandle.h` include → `"Mass/EntityHandle.h"` before next engine release · re-baseline `entity_spawn_80_us` on the nightly hardware path when the first real perf gate lands (T2.9).

---

## Doc map

| Path | What it is | Status |
|------|-----------|--------|
| `Docs/AGENTS.md` | Standing agent protocol — prime directives, run protocol, errata flow, report standard, doc conventions, brief template | **Living** |
| `Docs/README.md` | This file — status, map, read orders | **Living** (update with every phase) |
| `Docs/spec/architecture.md` | System architecture + ADR decision log (ADR-01…17) | **Living spec** |
| `Docs/spec/roadmap-and-testing.md` | Build order (Phase 0–6), test gates T0–T6, scenarios S1–S7, automation strategy | **Living spec** |
| `Docs/spec/reference-machine.md` | Perf-baseline hardware record (R10) | **Living** |
| `Docs/reference/engine-api-5.8.md` | Pinned UE 5.8.4 API reference: Part A audit (C1–C12 verdicts), Part B operational digest (§4.0–§4.13), Part C sources | **Living reference** |
| `Docs/reference/mcp-playbook.md` | Epic Unreal MCP operating manual — session lifecycle, call shape, toolsets, quirks ledger | **Living reference** |
| `Docs/reference/environment.md` | Install/project/VCS/config facts + facts-correction ledger | **Living reference** |
| `Docs/phases/pre-phase-0/` | brief · progress · report (environment scaffold run) | **Archive** |
| `Docs/phases/phase-0/` | brief · progress · report · audit (foundation run + post-run audit) | **Archive** (report §7 = Phase 1 entry point) |
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
