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
(2026-10-28 09:28).

## Follow-up session (same day): bisect + fixes -- 3 of 5 green

A same-day bisect (build+run A/B cycles on this machine, `5b57c88` = 21/21
verified twice) pinned each failure to a commit and produced `a7a0cd1`:

| Failure | Pinned to | Root cause | Fix | Tier |
|---|---|---|---|---|
| CombatSmoke | `c3e0a96` | the record-gated waiter press: formations interpenetrate before the first record, waiter ring targets flip to the far side, pairing starves (paired=0 / 2400 ticks) | gate removed -- unconditional waiter press restored (the delay it targeted is separately addressed by MemberClamp) | GREEN |
| S4_WidthOverride, S5_MarchRefront | `5e46a1b` | StragglerPace throttles on MAX slot error: a commanded re-front's 12-18 m edge walks floor the anchor at 0.25x for the whole walk | throttle reads the new `MeanSlotErrorM` (block-level dress metric; drags/choke queues still cross DressBreak) | GREEN |
| worker cvar visibility (hygiene) | `c3e0a96` | plain `GetValueOnAnyThread()` from workers reads the render-thread shadow slot (slot 1); `SetInternal` writes slot 0 only -- runtime flips are invisible to worker gates in `-game -nullrhi` | `GetValueOnAnyThread(true)` (bForceGameThread) = engine-blessed slot-0 read | no functional change in current fixtures |

Tier evidence at `a7a0cd1` (clean tree): **unit 69/69, func 19/21, funcon 19/21**.

## Still deferred: S4_ShieldWall + S5_Mob (melt under defensive intents)

Characterized to the subsystem but not fixed -- needs the owner's design input.
Symptom: when the DEFENDER unit is idle it now receives defensive intents
(`c3e0a96`'s demo-fix3 table) and claims the engagement: the defender's own
counter-pairs work (80+ attacker deaths processed, rosters shrink), but the
ORDERED attacker unit ends up holding **zero pairs** (unit-split resolution
census: `U2 valid=0 rolls=0` across the whole 240 s budget). The melt tests
premise a passive defender; with an active defender the attackers' offensive
pairing is starved entirely. Verified NOT the cause: the engaged-march halt
(probe: creep and disable both fail), the waiter gate (already removed), the
intent table alone (probe: disabling it at `c3e0a96` still fails -- the
starvation survives), and the worker cvar reads (fixed above; the fixtures
never flip those cvars at runtime). Starvation mechanism bounded to the
assign/seam record-pressure bookkeeping under defensive pairing; the probe
runs that would pin the exact dropped-assignment site are sketched in the
bisect history (`RESDBG`/`PROBESEEK` instrumentation, removed from this
commit). Design question for the owner: what "defends itself" (demo-fix3's
"front rank pairs, presses, and rolls") should mean for a FORMED defender
facing an ordered attacker -- defensive hold-and-fight vs the current
seek-the-aggressor-line.

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
default-value fold-in (owner's nightly, per the movement report's checklist) -- plus
the ShieldWall/Mob design decision above.
