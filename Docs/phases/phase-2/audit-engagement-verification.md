# Phase 2 Audit -- First-Pass Verification Record (2026-10-28)

Companion to `audit-engagement.md`. This file records what happened when the audit's
patch series was applied and **executed on the UE machine** (the audit environment had
no build; every "must re-run" note in the series is discharged here, except where
explicitly marked open).

| | |
|---|---|
| **Date** | 2026-10-28 |
| **Base** | `5e46a1b` ("Formation fix") |
| **Applied** | The 9-patch series as commits `362b948`..`a79b13e` (one commit per patch, `git am`, applied cleanly with no conflicts) |
| **Machine** | ref-i7-9700F-1660Ti (`Tools/CI/last_run.json` host record), UE 5.8.4 |

## Fixes to the audit's own patches (found by building/running)

1. **B-8 compile slip** (`413d9b2`): the `BodyLabelZCm` constant was placed inside
   `namespace DemoUi` in `DemoTypes.h` but the two projection call sites in
   `DemoPlayerController.cpp` were left unqualified -- C2065 at both, caught by the
   first editor-target build. Fixed by qualifying `DemoUi::BodyLabelZCm`.
2. **Pre-existing CloseGaps inverted assertion** (`a308b67`): unit tier's first run
   failed `Battle.Unit.Slots.CloseGaps` at
   `MassBattleSlotAssignerTests.cpp:594`. The fixpoint case's two core assertions were
   jointly unsatisfiable as written (`TestEqual` pins entity 5 walking 5->3->1, which
   *occupies* slot 1; the following `TestFalse` demanded slot 1 vacant). The patch
   series never touched `BattleSlotAssigner.{h,cpp}` or the test file (verified by
   `git diff 5e46a1b..HEAD --stat` on those paths) -- the test has been red since it
   was written. Matcher corrected to `TestTrue` per its own label and the seam
   contract.

## Tier evidence (discharges the series' "MUST re-run" notes)

| Tier | Result | Covers |
|---|---|---|
| **unit** (`Battle.Unit+Battle.Data+Battle.Smoke`) | **69/69 green**, engine exit 0 | patches 3/4/6 goldens (T2.1), T2.3 assign invariants, patch 5's test removal |
| **func** (`Battle.Func`, flag-off) | 16/21 -- see below | patches 1/2/7 scenarios (CombatSmoke, S5_Mob), patch 4's T2.8 DeterminismTwoRun **passed** |

The 5 func failures are **pre-existing and identical with and without the series**:
A/B run at base `5e46a1b` (clean build, same tier command) fails the same five:
CombatSmoke, S4_ShieldWall (240 s timeout), S4_WidthOverride ("march timed out"),
S5_MarchRefront ("march timed out"), S5_Mob ("no pull-down, exposed=3 waiters=46").
Notable signatures: CombatSmoke fails with "contact without pairs (paired=0
ringing=12)" -- contact detection sees the ring, the assign pass produces zero pairs.

All five fail at the audit's own subject HEAD, so they are **outside the audit
series' blast radius** (the series' combat-path changes are value-identical by
construction and the A/B confirms it behaviorally). Timeline: the engagement run's
"func 21/21" (status block, 2026-10-27) predates `5e46a1b` "Formation fix"
(2026-10-28 09:28, +305 lines `BattleSlotAssigner.cpp`, +100 `ServoProcessor.cpp`,
+126 `FormationSlotResolveProcessor.cpp`) -- that commit is the prime suspect for the
march/pairing regressions, but root-causing it is its own task and was **deferred**
(owner's fresh work; not part of the audit mandate).

`funcon` (flag-on tier) was **not** run: with 5 scenarios already red flag-off, its
results would not be interpretable. Re-run both after the formation regression is
fixed.

## Also executed from the audit's deferred list

- **PR-5** (D-5): executed as `2230eb5` -- `Tools/agent/` deleted (13 one-shot
  session scripts, hard-coded absolute paths, some targeting moved files), with the
  one-line policy note in `Tools/CI/README.md`. Nothing referenced the directory
  (grep over md/bat/py/cs/Jenkinsfile). Rollback: `git revert 2230eb5`.
- The 9 `.patch` files are filed under `Patches/` (the series README references them
  there; they were previously only in the transport workspace).

## Still deferred (need editor/MCP/owner, see `Patches/PRs/`)

PR-1 (data-driven scenario composition), PR-2 (editor-owned IMC), PR-3 (share tile
extent), PR-4 (registry-home locomotion maps + assign-pass extraction), and the D-3
default-value fold-in (owner's nightly, per the movement report's checklist).
