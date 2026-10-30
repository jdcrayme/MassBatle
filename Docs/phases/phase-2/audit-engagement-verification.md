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

### CORRECTION (2026-10-30 session): the starvation's mechanism is now pinned -- two defects, not record-pressure bookkeeping

Instrumented reruns on `4357321` (fresh func tier reproducing the same four
failures; the decisive evidence logs are `Tools/CI/probe_s4d.log`,
`probe_scan2.log`, `probe_full.log`, `fix2_s4.log`) supersede the
"bounded to the assign/seam record-pressure bookkeeping" characterization
above. The root, in causal order:

1. **The assign write-back delivered seeker states to the wrong entities.**
   `AssignPairsProcessor.cpp`'s write-back advanced its cursor by ONE PER
   CHUNK while using it as the offset into the flattened seeker array:
   chunk k read `Seekers[k + Row]` instead of the cumulative seeker offset.
   Whenever an earlier chunk's seeker count differed from 1 (the melt
   fixtures: one ordered unit seeking, one idle unit not), every later
   chunk's states landed shifted -- entity N received entity N+1's state,
   and once the idle unit's defensive intents made it seek too, chunk 1
   received the DEFENDER's states: the ordered unit's entities carried
   defender states and **killed their own teammates** (fresh 2026-10-30
   full-tier log: S4 = 20 sergeant-on-sergeant kills, S5 = 16
   peasant-on-peasant; first teammate kill ~2 s after the attack order,
   while 20+ m apart). `PROBE_WB_MISALIGN` (50-line cap, `probe_s4d.log`)
   shows the exact +1 shift; `PROBE_SEAM_SAMEUNIT`/`PROBE_GATHER_SELFINTENT`
   never fired -- the seam's unit filter is clean, the write-back was the
   only corrupt writer. Introduced with the task-6 assign pass (`4e76308`);
   masked until demo-fix3 (`c3e0a96`) made both units seek, which is why the
   melts surfaced on 2026-10-28 and were misattributed to the defensive
   intents.

2. **The write-back cursor was SHARED between two different index spaces --
   the complete root.** The interim fix (advance the cursor by the chunk's
   seeker count) corrected the Seekers offset but kept the cursor driving
   the `ChunkIndices` lookup too: advanced past the chunk-table's end, every
   chunk after the first hit the out-of-bounds guard and its rows were
   silently dropped -- the ordered unit's fresh pairs never reached the
   fragments at all (`PROBE_WB` owner counts, `Tools/CI/probe_full.log`:
   peasant stamps 20-40/frame, sergeant stamps zero, while the seam assigned
   both at 1.24 m). That state masked as "the raw seam cannot sustain the
   ordered side's pairing" (an earlier draft of this correction blamed
   pairing-gate vs queue-park geometry and idle-unit wander from
   `probe_scan2.log`; those measurements were real but downstream of the
   dropped write-back). The complete fix splits the cursors: ChunkCursor
   walks ChunkIndices (one entry per chunk), SeekerCursor walks Seekers
   (one entry per seeker). Landed as `21d73d1` + `2c81bc0`.

Post-fix state (func tier at `2c81bc0`: unit 80/80, func 19/23 with
PairLifecycle green again and exactly the four known failures, signatures
changed):

- **The melt machinery runs end-to-end for the first time.** S4_ShieldWall's
  fresh signature is `wallHeld=0 (budget 2.5 over 2 samples) melted=1
  (budget 6.0 over 10 samples)` with the wall thinning under legitimate
  kills (80 peasant deaths, 6 sergeant; pressure 3.0 -> 6.0 as coverage
  drops -- the EC2 curve the test wants, registering). The test now fails
  only because the wall phase collapses inside the 15-tick sample cadence
  (2 wall samples < 3). The known balance lever: `FBaseStatsFragment`
  Lethality/Shield still ride their spawn defaults (1.0) instead of the EDU
  rows (0.35/0.2 -- the phase-3 report's disclosed T6.1 gap), roughly
  doubling the sergeants' kill rate; stamping them (T6.1) or re-scaling the
  wall-phase sample cadence are the owner's recalibration options.
- **S5_Mob's signature improved from the corrupted baseline**: `no pull-down
  (240 s; exposed=4 waiters=58)` -- the ordered mob now pairs (was
  exposed=5 waiters=44 under the corruption, exposed=0 waiters=0 under the
  half-fix) but the pull-down still doesn't land inside the band: the
  knights' counter-pairs (hit chance 0.667 x default Lethality 1.0 vs the
  peasants' 0.11) kill the mob faster than the mob can pull down 8-men
  units. Same T6.1/stamping lever plus the S5 balance decision.
- **Friendly-fire kills are gone** (all kills legitimate enemy kills in
  every post-fix run).
- **DeterminismTwoRun unchanged** (`firstDivergence=0`, the pre-existing
  mm-scale two-run blocker; hex-float servo tracing remains the next step).
- **S4_WidthOverride unchanged** (`separation 0.285 m` vs the 0.300 bound;
  the documented tighten-or-accept owner call).

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
