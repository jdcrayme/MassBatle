# AGENTS.md — MassBattle Standing Protocol

Invariants for **every** autonomous run on this project, regardless of phase. Phase briefs **add** phase-specific scope, decisions, and risk watchlists on top of this — they must not restate it.

| | |
|---|---|
| **Precedence** | Reality > this file > the active phase brief > reference docs > archived records. Owner decisions in a brief (§3.1-style tables) are pre-decided: implement as written, never reverse or re-litigate. |
| **Applies to** | Humans preparing briefs (§8) and autonomous coding agents (§1–§7). |

---

## §1. Session bootstrap (read order)

1. `Docs/README.md` — current status + doc map + task-based read orders.
2. This file.
3. The active phase brief: `Docs/phases/phase-N/brief.md`.
4. Only the spec/reference sections the brief cites — **token discipline**: never read the spec docs end-to-end (~50k tokens); open only cited sections; never re-research a fact a reference file already states — cite it (e.g. "engine-api Part A §1") in the progress log instead.

## §2. Prime directives

1. **Reality wins.** If reality contradicts a documented fact, reality is right: correct the owning document (§4), log the discrepancy with evidence, keep moving. Never fabricate success; never edit a spec doc to make reality fit.
2. **Evidence before claim.** Every "done" carries tool evidence — a log line, test JSON (`"state":"Success"`), `is_dirty=false`, a git hash. No exceptions.
3. **Scope lock.** Build exactly what the brief's task plan defines. Do not pull future-phase work forward. Deviations only where reality forces them, each logged with reason. The brief's "explicitly out of scope" list is binding.
4. **Stop at DoD.** When the brief's Definition-of-Done checklist is verifiably checked, write the handoff report (§5), make the final commit, and stop. Do not start the next phase.

## §3. Operating protocol

- **Progress log**: `Docs/phases/phase-N/progress.md`, created at kickoff, written **as you go** — one line per completed step: `[taskN.M] what was done → verification result`. Session header records date, engine, project, protocol. Discrepancies get their own dated entries at the top.
- **Commits**: one commit per completed task, prefix `phaseN:`. If file coupling forces two tasks into one commit, disclose it in the handoff report.
- **Failure policy**: retry a failed step up to 3 times with diagnosis between attempts (read the returned error/logs — most failures are schema/API mismatches). If still failing after two distinct approaches: stop that task, write the blocker to the progress log with full error text, move to the next independent task, and surface all blockers in the handoff report.
- **Division of labor**: text/file edits → direct file tools; anything needing editor state (levels, assets, automation runs) → MCP; anything needing the editor closed (UBT of new modules) → shell.
- **Editor restarts are the scarcest resource** — batch all file-side work before each restart (see `Docs/reference/mcp-playbook.md` §3.7). MCP calls are serial (playbook §3.1).
- **Data files**: ASCII + LF only; em-dashes forbidden (use `--`).
- **Code conventions**: all game C++ in the plugin module (`Plugins/MassBattle/Source/MassBattle/`); the game module `MassBattleGame` stays empty EXCEPT the editor-authored demo (2026-10-25, owner decision): selection/UI/camera demo code lives in `Source/MassBattleGame/` and consumes the plugin's public API only (A2 orders + registry reads). Test naming, test-mode contract, and the pure-math seam live in the roadmap B.0 — link, don't restate.

## §4. Corrections & errata flow (replaces ad-hoc "fix the brief in place")

1. **Verified facts have one home**: engine API → `Docs/reference/engine-api-5.8.md`; MCP → `Docs/reference/mcp-playbook.md`; environment/install → `Docs/reference/environment.md`; hardware → `Docs/spec/reference-machine.md`.
2. **During a run**: a confirmed discrepancy is corrected in the owning reference file (or, for brief-fact digests not yet extracted, the brief's line in place) **and** logged in the progress log with evidence. Corrections tables (like the audit's C1–C12) are the update format.
3. **Briefs are frozen at kickoff** — they are the record of what was executed. Post-phase corrections never rewrite a completed brief; they land in the reference file the fact now lives in.
4. Phase reports must not accumulate "tool quirks" or environment facts — those go to the playbook/environment files (§7).

## §5. Handoff report standard (required sections, in this order)

Both completed runs converged on this shape; it is now binding. Write to `Docs/phases/phase-N/report.md`:

1. Header: date, agent run, engine pin, project, status line ("all tasks complete / blockers open").
2. **What was built** — paths, grouped by deliverable.
3. **Test evidence** — verbatim tool output (log lines, test JSON), condensed but real.
4. **Facts corrections & deviations** — what reality overruled, each with reason (cross-reference the reference-file ledger entries created per §4).
5. **Where phase N+1 should start** — read order, ready hooks, re-baseline reminders, deferred items.
6. **Open blockers** — or "None".
7. **DoD checklist** — the brief's §7 list with real checkboxes and evidence pointers.

## §6. Definition of done — standing additions (per capability)

Beyond the phase brief's own DoD, roadmap B.8 applies to every new capability (unit tests over the math, scenario test, debug overlay hook, metric, regression-checklist entry, CVAR feature flag). No capability is "done" on a green compile alone.

## §7. Documentation conventions

- **One fact, one home.** Reference files own their facts; everything else links. If you find the same fact in two places, fix the duplication on the spot.
- **Cross-references are repo-root-relative** and always start with `Docs/…` (machine-resolvable, independent of the citing file's location).
- **Shorthand table** (historical docs use these; map via this table):

  | Shorthand | Canonical file |
  |---|---|
  | arch | `Docs/spec/architecture.md` |
  | roadmap | `Docs/spec/roadmap-and-testing.md` |
  | findings | `Docs/reviews/plan-review-findings.md` |
  | BRIEF | the phase's own brief (archived under `Docs/phases/`) |
  | the audit | `Docs/phases/phase-0/audit.md` |
  | api-audit / §4.x | `Docs/reference/engine-api-5.8.md` (Parts A / B) |

- **Where new artifacts go**: phase lifecycle files (brief, progress, report, audit) → `Docs/phases/phase-N/`; durable verified facts → `Docs/reference/`; decisions and specs → `Docs/spec/` (ADR rows go in the architecture doc's decision log); independent review evidence → `Docs/reviews/`.
- **Every new doc is added to the `Docs/README.md` map in the same commit** that introduces it.
- **Spec docs are read-only for executing agents.** Proposed spec patches are made only where a brief explicitly authorizes them (the A7 pattern), and are called out in the report.
- Naming: phase folders `phase-N` (kebab-case for multi-word phases like `pre-phase-0`); version-pinned references carry the version in the filename (`engine-api-5.8.md`).

## §8. Phase brief template (for the brief author)

Keep briefs self-contained for the run, but let them *point* instead of *restate*:

1. **Header block**: companion docs, engine pin, run mode, priority directive, owner-approval status.
2. **§0 Mission + read order + token discipline** — exact sections to read, nothing more.
3. **§1 Phase facts digest** — pre-verified, ID'd facts **new** to this phase (environment/API facts already in `Docs/reference/` are cited, not copied).
4. **§2 Pointers** into `Docs/reference/mcp-playbook.md` (one line: "operating rules live there").
5. **§3 Phase ground rules** — only phase-specific additions to `Docs/AGENTS.md`; plus the owner-decision table (A# IDs) and the phase risk watchlist.
6. **§4 (only if new API territory)** — pre-researched digest for symbols not yet in `Docs/reference/engine-api-5.8.md`; plan its audit and merge results back into the reference file (§4 flow).
7. **§5 Task plan** — each task: Goal → Steps → Acceptance check → Commit. Highest-priority deliverable is Task 1.
8. **§6** gate→implementation map · **§7 DoD checklist** · **§8 System prompt** — now five lines, not a page:

   ```text
   You are the MassBattle Phase N implementation agent.
   Read Docs/README.md, then Docs/AGENTS.md, then Docs/phases/phase-N/brief.md.
   Execute the brief autonomously under the standing protocol; brief §3.1
   decisions are owner-approved. Stop at the brief's DoD (§7) after writing
   Docs/phases/phase-N/report.md and the final commit.
   ```

---

*Standing protocol consolidated 2026-10-25 from pre-phase-0 brief §0/§2, phase-0 brief §0/§2/§3/§8, and the handoff-report shapes of both completed runs. Changes to this file are owner decisions — propose via a brief's decision table.*
