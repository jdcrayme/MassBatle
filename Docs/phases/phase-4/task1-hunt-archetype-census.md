# Phase 4a -- Task 1 hunt, session 3: the section-3 residue (archetype count)

| | |
|---|---|
| **Target location** | `Docs/phases/phase-4/task1-hunt-archetype-census.md` (add to the `Docs/README.md` map in the same commit -- AGENTS §7) |
| **Companion docs** | `Docs/AGENTS.md` (protocol) · `Docs/phases/phase-4/progress.md` (session-2 record, discrepancies 2026-11-01) · `Docs/spec/roadmap-and-testing.md` Part B (B.4/B.5) |
| **Engine pin** | UE 5.8.4 (ADR-13) |
| **Run mode** | Autonomous implementing agent, one session; owner decisions H-1..H-4 in §6 are pre-decided once signed |
| **Priority directive** | Close the DeterminismTwoRun section-3 residue. Everything else stays out (§8). |
| **Status** | **EXECUTED 2026-11-02** -- see §9 (the execution record). The §0 artifact model was CONFIRMED by the §2 probe and the census fix landed (H-1 option A, H-2 yes, H-3 keep); the §2 falsifier row 4 then fired (a real residue survived the census at samples 31-40) and the §3 escape hatch ran: the live defect was found and fixed -- NOT in the hash, in the morale pass's reset-ordering (the empty-world fire re-armed the cadence accumulator mid-reset; run 2's morale phase shifted one tick and a tick-identical kill straddled the pass boundary, forking the rout cascade). `Battle.Func.DeterminismTwoRun` PASS 40/40 isolated AND in-gate; unit 85/85; func 25/27 = the two known (S4_WidthOverride, S5_Mob). H-4 (the roadmap B.4/B.5 wording) remains an owner call -- proposed patch recorded in §9. |

---

## 0. The hypothesis to verify (read this first)

Session 2 recorded: "the residue = section 3 ARCHETYPE COUNT (some tag-set
membership differs per run) cascading into the shared fragments -- the next
hunt's entry point" (`Docs/phases/phase-4/progress.md`, 2026-11-01 entry).

Code reading says the more likely truth: **the section-3 divergence is a gate
design artifact, not a live determinism defect.** The section-3 term hashes
`EntityManager.DebugGetArchetypesCount()` (`Plugins/MassBattle/Source/MassBattle/Private/BattleStateHash.cpp:92-102`)
-- a **session-cumulative, process-history counter**. Archetypes are created
on demand and never destroyed within the process (entity destroys leave them
resident; empirically proven by this repo's own warm-up comment:
"Run 1 previously CREATED the ranged compositions (first ranged spawns of the
session) while run 2 reused them -- every sample diverged on the count
(measured 2026-10-29)", `Plugins/MassBattle/Source/MassBattleTest/Private/BattleFuncTestActor.cpp:5896-5904`).

The asymmetry derivation, from the test's structure (both runs replay the
identical mixed fixture, same RNG root 20261027 re-seeded in `DestroyFixture`,
`BattleFuncTestActor.cpp:5870-5873`):

```
A(k) = | warmup_set + creations(run1, ticks 1..60k) |            -- run 1 sample k
B(k) = | warmup_set + creations(run1, FULL) + creations(run2, 1..60k) |

identical trajectories  =>  creations(run2, 1..60k) = creations(run1, 1..60k)
                        =>  B(k) = | warmup_set + creations(run1, FULL) |
                        =>  B(k) >= A(k), equality only AFTER run 1's last
                            first-time composition creation
```

So under **perfect** determinism, section 3 diverges at every sample between
run 1's first and last first-time composition creation. In this fixture the
only mid-battle composition creators are the death variants (§1 table): the
process-wide first `{infantry, archer} x {+FDeadTag, +FDeadSignaledTag}`
compositions -- up to four milestone events. Kills are NOT the tell: `bDead`
is hashed in section 2, and section 2 matches, so kill timing agrees at
sample granularity. The count cannot agree, because run 2's count includes
run 1's whole session history.

This also predicts the observed "cascade": it is a **prefix artifact** of the
bisector, not independent evidence. `EndSection` (`BattleStateHash.cpp:43-50`)
hashes the byte stream **so far** (cumulative prefix) -- one diverging section
forces every later section hash to differ. Sections 4-10 carry zero
independent signal in the current output.

Two corollaries worth logging when verified:

- **Contract violation in the hash's own header.** `BattleStateHash.h:2`
  declares "no non-sim state". The cumulative archetype registry IS process
  history -- the count term violates the file's own invariant. Removing it
  aligns the code with its contract.
- **Gate-order dependence / flakiness mechanism.** The func gate runs many
  tests in ONE process on the pinned map. Earlier tests (UnitDeathPath,
  CombatSmoke, S4/S5, garrison variants) pre-create death compositions, so
  run 1's milestone count can be 0..4 depending on test order -- the gate's
  DeterminismTwoRun outcome has been order-dependent by construction. This is
  a candidate explanation for adjacent flakes and must go in the progress log
  when confirmed.

The session therefore has TWO possible exits, decided by evidence (§2):
(a) the probe confirms the model -> fix the hash term (§4) and the bisector
(§5); (b) the probe contradicts it -> a real live drift exists, run the
dump-diff hunt (§3). Both exits are fully specified so the session cannot
dead-end.

---

## 1. Verified code facts (the mutator table is exhaustive)

Grep-verified on the public tree @ `c4ebf97`; re-verify at the working tree
before starting (AGENTS §2, reality wins).

| # | Fact | Where |
|---|---|---|
| F1 | Section 3 folds `DebugGetArchetypesCount()` only (a count, no membership) under `WITH_MASSENTITY_DEBUG`, else 0 | `BattleStateHash.cpp:92-102` |
| F2 | `EndSection` hashes the cumulative byte prefix (prefix-cascade bisector artifact) | `BattleStateHash.cpp:43-50` |
| F3 | Sections: 1 bookkeeping, 2 transforms+HP+dead, 3 archetype count, 4-8 shared stats/formation/garrison/combat/ranged, 9 projectiles, 10 morale; sections are cumulative-prefix hashes | `BattleStateHash.cpp:52-280`, `BattleFuncTestActor.cpp:5991-5995` |
| F4 | The ONLY runtime composition mutators in the plugin are `AddTag<FDeadTag>` (3 sites) and `AddTag<FDeadSignaledTag>` (1 site) | see F4.1-F4.4 |
| F4.1 | melee kill -> `Context.Defer().AddTag<FDeadTag>(Pair.Target)` | `Private/CombatResolutionProcessor.cpp:138` |
| F4.2 | projectile kill -> `EM.Defer().AddTag<FDeadTag>(VictimEntry.Entity)` | `Private/ProjectileStepProcessor.cpp:318` |
| F4.3 | exec kill -> `EM.Defer().AddTag<FDeadTag>(Entity)` | `Private/BattleExecCommands.cpp:391` |
| F4.4 | death observer -> `Context.Defer().AddTag<FDeadSignaledTag>(Entity)` | `Private/BattleDeathPath.cpp:77` |
| F5 | Death flow: kill -> FDeadTag -> observer (registry-driven, `ObservedTypes={FDeadTag}`, AddElement) -> deferred signal + FDeadSignaledTag -> receiver (PostPhysics, next frame) -> roster update, GAS events, morale bookkeeping, deferred `DestroyEntity` | `BattleDeathPath.cpp:30-31, 54-55, 125-254` |
| F6 | NO `RemoveTag` / `RemoveFragment` / mid-battle `AddFragment` anywhere in the plugin (trait-time `BuildContext` adds only); entity destroys: `BattleDeathPath.cpp:251`, `BattleRegistry.cpp:1273` (ResetBattleState), `UnitActor.cpp:75` (EndPlay) | grep evidence |
| F7 | `FDeadTag` is a REGULAR tag (ADR-15): regular tag add = archetype migration; sparse tags are chunk-granular and observer-incompatible (audit C11) -- so each death migration creates the dead variant archetype on first use | `BattleDeathPath.cpp:28-29`, engine-api Part A C11 |
| F8 | Morale machinery is composition-clean: `MoraleTickProcessor.cpp` has zero tag/fragment mutations (rout state lives in the registry `FUnitRuntimeState`, hashed in section 10) | grep evidence |
| F9 | Pair/charge fragments are spawn-immutable (Route A adds both at spawn; the hash zero-fills their absence) | `Private/BattleSpawnLibrary.cpp:299-300`, `BattleStateHash.cpp:151-206` |
| F10 | The gate pins the morale pair (Enable + DeterministicTick) and six det cvars; samples every 60 ticks over 2400; bisector prints per-section run1/run2 hashes at the first mismatching sample | `BattleFuncTestActor.cpp:5719-5732, 5939-6022` |
| F11 | The warm-up block exists ONLY because of this exact persistence mechanism (ranged spawn compositions, measured 2026-10-29) | `BattleFuncTestActor.cpp:5896-5904` |
| F12 | The cumulative archetype count is ALREADY watched outside the hash: `Battle.DumpMetrics` logs it (B.2/I2 rule) and the nightly soak prints `archetypes=N` | engine-api-5.8.md Task 12 note; phase-1 report soak line |

Implication of F4+F6: within one battle the composition set only GROWS (no
removals), through at most the four death-variant milestones. There is no
other candidate for a "tag-set membership differs per run" event in the
two-run fixture -- which is precisely why the artifact explanation fits and a
live-drift explanation would need a mutator the grep cannot see.

---

## 2. Task 1.S3-a -- the probe (do this first; ~10 minutes + one run)

Goal: confirm or falsify the §0 model BEFORE changing the hash (AGENTS §2,
evidence before claim).

Steps:

1. At both sampling sites (`BattleFuncTestActor.cpp:5941-5946` run 1,
   `:5969-5974` run 2), after `FBattleStateHash::Compute`, add one log line:

   ```
   UE_LOG(LogMassBattle, Display,
       TEXT("EXEC_RESULT Det2RunCensus run=%d tick=%d count=%d hash=%016llx"),
       RunIndex, TicksDone,
   #if WITH_MASSENTITY_DEBUG
       EntityManager.DebugGetArchetypesCount(),
   #else
       0,
   #endif
       Hash);
   ```

   (`RunIndex` = 1/2; get `EntityManager` the same way the hash does. Keep the
   line behind no cvar -- it is Display-level, cheap, and becomes the gate's
   permanent count telemetry after the fix, which is where the B.4 watch
   should live anyway.)

2. Also log the count ONCE at `StartTest()` before run 1
   (`BattleFuncTestActor.cpp:5876` area) -- this measures cross-test
   contamination (how many death variants earlier gate tests pre-created).

3. Run the test (isolated first: just `Battle.Func.DeterminismTwoRun`), then
   optionally under the full func gate.

Expected signature (model CONFIRMED) -- keep this table in the evidence:

| Observation | Expected under the artifact model |
|---|---|
| run 1 count trajectory | monotonically non-decreasing, at most +4 steps (fewer if StartTest's pre-run count already includes some death variants) |
| run 2 count trajectory | CONSTANT across all 40 samples, equal to run 1's final count |
| run 2 vs run 1 at the same sample | run 2 >= run 1 always; divergence begins at the first sample after run 1's first new milestone and ends after its last |
| sections 1-2 at the first diverging sample | match (kills agree at sample granularity) |
| full-gate vs isolated pre-run count | full-gate pre-run count higher (earlier tests pre-created variants) |

Falsifiers (any one -> the model is WRONG, go to §3 immediately):

- run 2's count is not constant (run 2 created something run 1 never did --
  impossible under identical trajectories).
- run 1's count decreases (archetypes destroyed -- contradicts F1/F11).
- sections 1 or 2 diverge at the first mismatching sample (a REAL live state
  defect -- this is the session-2 entry point actually being live).
- the count trajectories match at every sample yet the full hash still
  diverges (divergence lives in a later section's own content -- only visible
  after the §5 bisector fix; sequence: land §5, re-run, then re-evaluate).

---

## 3. Task 1.S3-d -- the escape hatch (only if the probe falsifies the model)

A real live drift means some composition differs between runs at a sample.
The mutator table (F4) is exhaustive, so the drift must come from one of the
four sites firing differently (a kill/signaling boundary flip) or from a
mutator outside the plugin (engine-side; then AGENTS §4 -- verify at the pin,
record in `Docs/reference/engine-api-5.8.md`).

Dump-diff procedure (carries session 2's tooling lessons -- explicit run tag +
per-entity tick counter, worker-thread interleaving defeats file-order
segmentation; `MSYS2_ARG_CONV_EXCL="*"` for map args):

1. Build a per-sample composition census dump (the §4 Option A census, before
   it becomes the hash term): one line per entity per sample is too much --
   dump per sample one line per DISTINCT composition bitset + instance count,
   run-tagged: `Det2RunComp run=1 tick=600 bits=0x... n=23`.
2. Run twice, diff the two logs at the first diverging sample: the differing
   `bits` line names the composition family and the population delta.
3. Map the bit difference to the type (the bitset layout is the §4 type
   list), then to the mutator site in F4. A kill-boundary flip (one run
   killed at tick t, the other at t+1) will show as a transient
   `n` difference that later re-converges while the count stays shifted --
   distinguish this from a permanent difference.
4. For a kill-boundary flip: the hp-damage write path and the Tally /
   CombatResolution ordering are the suspects; the fix pattern is the
   session-2 A13-canonical one (entity-keyed gathers, CHUNK-order bids,
   canonical containers) -- `FormationSlotResolveProcessor.cpp` and
   `AssignPairsProcessor.cpp` from commit `4ace397` are the worked examples.
5. Land the fix, then continue with §4/§5 anyway (the count term must STILL
   leave the equality -- §0's asymmetry applies regardless).

---

## 4. Task 1.S3-c -- the hash fix: retire the count term, adopt the live census

Owner decision H-1. The count term cannot stay in the equality hash in any
variant: it measures process history, not sim state (§0). Recommended: Option
A. Options B and C are recorded for completeness.

### Option A (recommended) -- project-owned live composition census

Replace the section-3 term: in a pass over the EXISTING sorted-roster walk,
fold per entity a fixed-width presence bitset over a project-owned type list,
then (optionally) a per-distinct-bitset instance count.

- The type list lives in ONE home: `kCensusTypes[]` in `BattleStateHash.cpp`
  (AGENTS §7, one fact one home). Initial contents = the Route A spawn
  composition (`FOwnerUnitFragment`, `FLoadoutFragment`, `FBaseStatsFragment`,
  `FHealthFragment`, `FCombatPairFragment`, `FChargeFragment`,
  `FRangedStateFragment`, the movement-stack fragments on the Route A list),
  the trait tags (`FInfantryTag`, `FCavalryTag`, `FMeleeTag`, `FBowTag`),
  `FDeadTag`, `FDeadSignaledTag`, `FTransformFragment`.
- Presence reads: `FMassEntityView::HasTag<T>()` /
  `GetFragmentDataPtr<T>() != nullptr` -- the exact idioms the hash already
  uses (`BattleStateHash.cpp:69-76, 151-224`). No new engine API, no
  `WITH_MASSENTITY_DEBUG` dependency, Shipping-safe.
- Fold order: the roster walk is already canonical (sorted handles; the A13
  work). Fixed bit width per entity keeps the stream position-stable.
- Properties: symmetric across runs BY CONSTRUCTION (live state only);
  catches any live membership drift in project-owned types; still watches
  composition variety (a real B.4 improvement over a bare count).
- Maintenance rule (one line in the phase checklist going forward): any new
  fragment/tag that can join a battle composition extends `kCensusTypes[]`.
  The §5 guard test enforces it for the death path.
- The B.4 unbounded-growth watch loses nothing: the cumulative count stays a
  METRIC -- the §2 probe line becomes permanent telemetry, and
  `Battle.DumpMetrics` + the nightly soak `archetypes=N` line already carry
  it (F12).

### Option B -- engine-debug archetype census (fallback only)

Enumerate archetypes under `WITH_MASSENTITY_DEBUG` (candidate APIs:
`DebugGetArchetypes()` / archetype descriptor access -- NOT recorded in
`Docs/reference/engine-api-5.8.md` yet; adopting B requires the §4
verification flow at the pin first), hash a canonically SORTED census
(sorted by composed type-name string -- the EM's archetype list is
creation-ordered, so an unsorted fold would reintroduce an order dependence,
the exact disease being cured). More general (covers engine-owned types),
but heavier, debug-gated, and blocked on new engine facts. Adopt only if §3
ever shows drift in types outside the project-owned list.

### Option C -- warm-up extension only (band-aid, not a fix)

Extend the warm-up to pre-create the death variants (spawn, kill one
infantry + one archer, let observer + receiver flush one frame, destroy,
then proceed). Minimal diff, but: a maintenance trap (every future
composition-creating feature must extend it), still order-dependent on which
tests ran earlier in the gate process, and leaves the hash blind to live
membership. Acceptable only as a temporary gate-unblocking measure while
Option A lands in a following session -- do not stack it on top of A.

---

## 5. Task 1.S3-b + 1.S3-e -- bisector fix and regression guard

### 1.S3-b: per-section bisector hashing (owner decision H-2; recommend: yes, same session)

`EndSection` (`BattleStateHash.cpp:43-50`) hashes the cumulative prefix.
Fix: record each section's start offset and hash each section's OWN byte
slice into `OutSectionHashes`. Term order and section indices stay stable
(the bisector's 1..10 names keep their meaning). No goldens are stored
anywhere (the bisector only prints), so there is no regolden cost. After
this, "section N DIFF" lines are independent evidence -- the precondition
for ever trusting a later-section signal (and for the §2 falsifier row 4).

### 1.S3-e: the guard test (unit tier; target 85/85)

Add one unit test (suite `Battle.Unit.StateHash`, or the existing state-hash
suite) with two assertions:

1. **Liveness:** spawn a minimal unit, compute the census; kill one entity
   through the exec path (`BattleExec::KillEntity`, the F4.3 site), flush the
   observer + receiver frame, recompute. Assert the census CHANGED (the term
   sees live composition change) while the cumulative count metric also grew
   -- proving the new term is sensitive to exactly the machinery that
   produced the residue.
2. **History-independence (the artifact killer):** reset, then run the same
   mini-battle twice in one process, with battle 2 preceded by an EXTRA
   throwaway kill (a spawned sacrificial entity killed + destroyed between
   the battles, seeding one additional composition). Assert per-sample
   census equality across both battles. This is the exact process-history
   asymmetry that broke section 3 -- the test fails on the old count term's
   semantics and passes on the census. If anyone ever reintroduces a
   cumulative term into the equality, this test catches it.

### Acceptance (the gate must go green in BOTH configurations)

- `Battle.Func.DeterminismTwoRun` PASS, samples=40/40, in isolation AND at
  its position in the full func gate (the order-dependence check).
- Unit tier 85/85; func tier 25/27 (DeterminismTwoRun clears; S4_WidthOverride
  and S5_Mob remain, independent sessions -- §8).
- Per-commit gate + Jenkins `massbattle-gate` green.

---

## 6. Owner decision table (pre-decided once signed)

| ID | Decision | Options | Recommended |
|---|---|---|---|
| H-1 | The section-3 replacement | A project-owned live census / B engine-debug archetype census / C warm-up extension only | **A** |
| H-2 | Land the per-section bisector fix in the same session | yes / defer | **yes** (no regolden cost; unlocks independent section evidence) |
| H-3 | The warm-up block after the census lands | keep / delete with evidence | **keep for 4a** (harmless, still correct); delete-candidate at Phase 5 when corpse policy changes the death-composition picture |
| H-4 | Roadmap B.4/B.5 wording ("archetype count" -> "live composition census in the hash; cumulative count as a metric") | owner edits the spec line / authorize the agent patch (the A7 pattern) / leave spec untouched | **owner edits or authorizes** -- spec docs are read-only for executing agents (AGENTS §7) without explicit authorization |

---

## 7. DoD checklist

- [ ] §2 probe landed, evidence captured: pre-run count + 2x40 sample count
      lines; the expected-signature table filled with REAL numbers (or the §3
      escape hatch executed to its own evidence).
- [ ] §4 hash fix landed (H-1 option); `kCensusTypes[]` in one home; count
      demoted to permanent gate telemetry.
- [ ] §5 bisector per-section fix landed (H-2).
- [ ] §5 guard test landed; unit tier 85/85 with evidence lines.
- [ ] DeterminismTwoRun PASS 40/40 isolated AND in-gate; func tier 25/27.
- [ ] Per-commit gate + Jenkins `massbattle-gate` green.
- [ ] `Docs/phases/phase-4/progress.md` session-3 entries (AGENTS §3 format:
      `[taskN.M] what -> verification`), including the §0 verdict disposition
      and the F-table re-verification note.
- [ ] `Docs/README.md` status row updated + this file added to the doc map
      (same commit -- AGENTS §7).
- [ ] New engine-API facts (if the §3 hatch was taken): recorded in
      `Docs/reference/engine-api-5.8.md` per AGENTS §4.
- [ ] Handoff report per AGENTS §5 (only if the session closes the whole
      Task 1; if the hatch reveals a live defect, the report records the
      blocker instead).

Commits: one per task, prefix `phase4a:` (AGENTS §3).

---

## 8. Explicitly out of scope (binding)

- `S4_WidthOverride` ("S4 refront assertions failed") and `S5_Mob` ("no
  pull-down (240 s; exposed=5 waiters=66)") -- signatures unchanged per
  session 2; their own hunts.
- The mixed clash+volley two-run equality residual (phase-3 dossier,
  `Tools/CI/det_*.log`) -- separate blocker; NOTE: if §2's model holds, that
  dossier should be re-read afterward -- its two-run comparison may carry the
  same cumulative-count disease if it folds any session-cumulative state.
- The movement-soak dress-gate failure on the staged nightly path, the
  SoakVolley=1 nightly, the morale-on baseline re-baseline, T2.9, the D3
  fold-in checklist -- nightly stack, separate session.
- Any new sim capability (AGENTS §2 scope lock; roadmap B.8 applies to the
  census only as the B.6-style debug hook + metric it already carries).

---

## 9. Execution record (2026-11-02, session 3)

**Verdict: the residue had TWO layers.** §0's artifact model was right about
section 3 -- and wrong that it was the whole story. The probe confirmed the
artifact exactly as derived; the census fix cleared the early samples; and
the falsifier row 4 then exposed the REAL defect underneath, which no amount
of hash surgery would have fixed.

- **[1.S3-a] The probe (CONFIRMED).** `det_census.log`: pre-run count=4;
  run 1 sampled 4 -> 6 (t720) -> 8 (t1800) -- the two +2 steps are the death
  variants ({infantry,archer} x {FDeadTag, FDeadSignaledTag}, created by the
  first kill of each composition family); run 2 CONSTANT 8 at all 40
  samples; sections 1-2 MATCH at the first diverging sample. Every
  expected-signature row hit (the last row's full-gate number came back
  EQUAL at 4 -- the count left the equality before it ever mattered).
- **[1.S3-c] The census (H-1 option A) landed.** `BattleStateHash.cpp`
  section 3 folds the per-entity presence bitset over `kCensusTypes[]` --
  the template argument list of `CensusBits<Ts...>` is the one home (20
  types: the Route A composition + the trait tags + both death tags);
  worker-safe, Shipping-safe, no `WITH_MASSENTITY_DEBUG` dependency. The
  count survives as telemetry (`Det2RunCensus` lines at the gate's
  pre-run + both sampling sites) + `Battle.DumpMetrics`.
- **[1.S3-b] The bisector landed -- with a plan-fact correction.** The plan
  said "10 sections"; reality had SIX `EndSection` calls (sections 4-8
  interleave per unit in one walk and shared one cumulative hash). The fix
  keeps the byte stream byte-identical and gives each of the 10 named
  sections its own accumulation buffer (the §5 plan's own-slice intent);
  the bisector's 1..10 names are now TRUE (they were mislabels before:
  hashes 4-6 were cumulative prefixes wearing names 4-10). Per-sample
  section-diff telemetry added (`Det2RunSec` one line per sample).
- **[1.S3-e] The guard test landed.** `Battle.Unit.StateHash.CensusGuard`:
  liveness (an exec-path kill flips the census slice) + history-independence
  (an extra throwaway kill between two identical mini-battles leaves the
  census EQUAL). Unit tier 85/85.
- **[1.S3-d] The escape hatch executed (falsifier row 4).** After the
  census fix the residue moved to `firstDivergence=11` (t720) and the
  per-section hashes showed 1 bookkeeping + 2 transforms + 3 census DIFF
  from t720 on -- a REAL live drift at the first kill. The instrumented
  hunt (`Battle.Det.RunTag` + `Battle.Det.Tick` cvars; DetPair/DetRoll/
  DetRngRoll/DetMor canonical-identity traces; evidence logs
  `det_census*.log`, `det_trace_s3*.log`, untracked like the det_* set):
  - kills #1-4 are TICK-IDENTICAL across the runs (t=681/682/693/694);
  - run 1's morale pass at t=694 saw spike=1.882/M=-0.093 (unit 3 breaks,
    routs at ~t=715, its attack stream stops); run 2's pass at t=693 saw
    spike=0.941/M=+0.954 (no break; kills continue at t=700-712);
  - the pass phases differed by ONE TICK (run 1 fired at 1+21k, run 2 at
    21k) because `ResetBattleState` zeroes the cadence accumulator
    mid-frame and the same frame's morale pass then fires on the emptied
    world and RE-ARMS 0.35 -- run 2 inherited the stale arm; the
    tick-identical kill #3 (t=693) straddled the shifted pass boundary;
  - **the fix (MoraleTickProcessor): an empty-world fire STAYS PRIMED**
    (cooldown -> 0, fire on the first populated tick) instead of consuming
    a period -- the cadence is now reset-ordering-immune. First isolated
    re-run: **PASS 40/40, mismatches=0** (`det_fix1.log`); in-gate PASS in
    the full func tier. The unit-tier morale goldens held (85/85).
- **Deviations/disclosures:** (1) the morale-cadence fix is beyond the
  plan's literal §4/§5 scope -- it is the §3 hatch's "land the fix" step,
  executed where the evidence pointed (AGENTS §2: reality wins); the DET
  instrumentation beyond the plan's one census line (RunTag/Tick cvars +
  four trace families) is the §3 dump-diff tooling, landed permanently,
  opt-in via `Battle.Det.RunTag` (0 = silent). (2) H-4: the roadmap B.4/B.5
  wording is READ-ONLY for this agent -- proposed owner patch: B.4/B.5 say
  "live composition census in the equality hash; cumulative archetype
  count as a metric (Det2RunCensus telemetry + DumpMetrics + the soak
  line)". (3) The two teacher crashes during instrumentation are recorded
  in the trace files' comments (worker-side cross-entity FMassEntityView is
  a data race; a dead shooter's handle needs IsEntityValid first).
- **Residual (the phase-3 dossier note, §8's last bullet):** the mixed
  clash+volley dossier (`Tools/CI/det_*.log`, phase 3) should be re-read
  under this record -- its two-run comparison predates BOTH fixes and its
  "mm-scale servo transient" signature is consistent with the same
  morale-phase straddle now fixed; no action taken this session.
