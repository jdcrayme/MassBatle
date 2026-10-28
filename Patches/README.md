# Audit patches (phase-2 engagement audit, 2026-10-28)

Branch: `audit/phase2-engagement` (from `main` @ `5e46a1b`).
Findings register: `Docs/phases/phase-2/audit-engagement.md`.

## Committed patch series (releases without UE access)

Each `.patch` file is one commit; apply in order with `git am`:

| # | Patch | Groups | Must re-run on the UE machine |
|---|---|---|---|
| 1 | `0001-...scaffolding.patch` | C-1 C-2 C-4 C-5 | full unit + func tier (deletions only) |
| 2 | `0002-...cull-static-maps.patch` | A-2 | func tier (soak/escalation metrics unchanged) |
| 3 | `0003-...body-radius-home.patch` | B-1 B-5 | T2.1 golden (formality) |
| 4 | `0004-...roll-hash-and-finddefender.patch` | B-2 A-4 | T2.1 golden + T2.3 + T2.8 DeterminismTwoRun |
| 5 | `0005-...t29-stub-and-guard.patch` | C-6 C-3 | unit tier (one test removed, expected) |
| 6 | `0006-...draincompositepool.patch` | A-3 | T2.1 golden (block removed, expected green) |
| 7 | `0007-...fixture-data.patch` | B-3 D-3 B-7 B-8 B-6 | S5_Mob + CombatSmoke (fixture path touched) |
| 8 | `0008-...doc-map.patch` | docs | none |

## PR writeups (require UE / MCP / owner decisions -- NOT in the patch series)

- `PRs/PR-1-data-driven-scenario-composition.md` (editor assets + PIE cycle)
- `PRs/PR-2-editor-owned-input-mapping.md` (IMC asset authoring + PIE verification)
- `PRs/PR-3-share-tile-extent-with-demo.md` (plugin API + UE test cycle)
- `PRs/PR-4-assign-seams-and-registry-state.md` (processor refactor + full tier)
- `PRs/PR-5-remove-agent-patch-scripts.md` (owner repo-policy call)

## Verification status

Static audit: all patches are brace-balanced, include-complete, and value-neutral by
construction (identical constants, same formulas, deletions of dead code). No UE build was
available in the audit environment; each commit message carries its "MUST re-run" note per
AGENTS §2 (evidence before claim).
