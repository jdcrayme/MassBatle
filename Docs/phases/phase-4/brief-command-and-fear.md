# MassBattle Phase 4b Brief -- Command & Fear (OrderLatency + TerrorAura)

| | |
|---|---|
| **Target location** | `Docs/phases/phase-4/brief-command-and-fear.md` (the name the `Docs/README.md` doc map promises; enters the map in its introducing commit -- AGENTS §7). Commit prefix `phase4b:` |
| **Status** | **FROZEN at kickoff, 2026-11-03, against `main` @ `90c3fb1`** (the 4a report filed: `Docs/phases/phase-4/report.md`). Freeze checklist §9: (1) the 4a report exists, its section 5 names the 4b entry points -- DONE; (2) H-4 applied -- DONE (the roadmap B.4/B.5 wording); (3) the owner marked §3.1 B-1..B-8 approved 2026-11-03 -- the rows carry the approval below; (4) §1 re-verified at the kickoff HEAD -- the F-ids re-cited with current line anchors; (5) every SLOT resolved (each carries its resolution); (6) the map row lands in this file's introducing commit. |
| **Companion docs** | `Docs/spec/morale-and-gas.md` (§1 scope, §3.1/3.2 data model, §4.6 geometric inputs, §4.12 tunables, §9 split rationale, §10 extension points) · `Docs/spec/ranged-stack.md` §4.8 (the RC7 order-serial guard) · `Docs/spec/roadmap-and-testing.md` (Part A Phase 4 note, B.0/B.5/B.7/B.8/B.9) · `Docs/AGENTS.md` (standing protocol) · `Docs/phases/phase-4/report.md` §5 (the 4a entry points: the ready hooks, the binding standing rules, the re-baseline reminders) |
| **Engine pin** | UE 5.8.4 (ADR-13). NO new engine-API territory expected: the GAS surface is digested (engine-api Part B §4.14), the queue is project-side. §4 is kept as an empty slot by design |
| **Run mode** | Single autonomous run, protocol per `Docs/AGENTS.md`; CI evidence via `Tools/CI/run_tests.bat {unit,func}`; nightly via `Tools/CI/nightly.bat` |
| **Priority directive** | The two ADR-37 mechanisms plus the climate data slice: (1) OrderLatency -- the order-delay queue that finally CONSUMES the attribute the closed set has carried since Phase 0; (2) TerrorAura -- the fear mirror of GeneralAura against enemy envelopes, with the EDU `frighten_foot`/`frighten_mounted` tokens joining the parse; (3) the climate-fatigue DATA slice only -- `hardy`/`very_hardy` -> `FatigueRateScale` (the live-but-unwired slot). The weather/terrain SYSTEMS stay Phase 5+ (spec §1; the extension-point table §10) |
| **Owner-approval status** | §3.1 B-1..B-8 **APPROVED by the owner 2026-11-03** (recorded in `Docs/README.md`'s status block and the 4a report) -- implement as written, never re-litigate |
| **Precondition** | The 4a run closed (its report filed) -- DONE. H-4 applied -- DONE. The two-run base GREEN (the report: PASS 40/40 with the full morale loop) -- DONE |

---

## 0. Mission + read order + token discipline

**Mission.** Command and fear from the machinery 4a already proved: a player
order takes effect when the unit's `OrderLatency` elapses -- not instantly --
and frighteners drain enemy morale exactly as the general inspires friends.
Both are consumers of the Phase 0-3 spine and the 4a morale pass, not new
systems. The architecture claim in one sentence: **the delay queue is
transient registry bookkeeping that releases orders on tick boundaries, and
TerrorAura is one more weighted term in the existing cluster-cadence morale
fold -- zero new attributes, zero new per-entity fragments.**

**Read order** (token discipline: nothing else; do not read the spec docs
end-to-end):

1. `Docs/README.md` status block, then `Docs/AGENTS.md`.
2. This brief, all sections.
3. `Docs/spec/morale-and-gas.md` -- §1 (the 4b scope rows), §3.1/3.2 (what
   already exists vs what 4b adds), §4.6 (the geometric-input discipline),
   §4.12 (the tunables posture), §9 (why 4b is its own run), §10 (the
   extension-point table 4b implements two rows of).
4. `Docs/spec/ranged-stack.md` §4.8 ONLY -- the RC7 order-serial guard, the
   pattern the delay queue generalizes.
5. Roadmap: the Part A Phase 4 run-split note, B.0 (test contract), B.5
   (determinism, post-H-4 wording), B.7 (the phase-4a entry: the cadence
   watch + the reset-ordering rule are BINDING here), B.8 (capability DoD),
   B.9 (how a gate row is amended).
6. `Docs/phases/phase-4/report.md` §5 -- the 4a entry points (read at
   kickoff; it exists and is the freeze basis).
7. `Docs/reference/engine-api-5.8.md` -- Part B §2-3 (processor facts) as
   needed; nothing new is expected.

**Do not** open the reference/playbook files end-to-end (AGENTS §1).

## 1. Phase facts digest (re-verified 2026-11-03 at the kickoff HEAD `90c3fb1`)

Every fact below was re-verified against the cited source TODAY (the
freeze-checklist step 4; the line anchors updated where the 4a close moved
them). Cite fact IDs in the progress log instead of re-reading.

- **F1** The OrderLatency spine is COMPLETE and inert: the GAS attribute
  (`Public/UnitActor.h:43` + accessors `:55`, enumeration
  `Private/UnitActor.cpp:241`), the registry attribute-mirror field
  (`Public/BattleTypes.h:178`), save v3 capture/restore
  (`Private/BattleRegistry.cpp:1356/1479` via `OrderLatencyAttr`,
  `Public/BattleSave.h:45`), the UnitSync cached slot
  (`Private/UnitActor.cpp:120`). ZERO new attributes this phase. The only
  new state is the QUEUE. *(Re-verified exact, all anchors unchanged.)*
- **F2** The value is unwritten and unconsumed: default `0.f` everywhere;
  the registry copies are save round-trip only; nothing reads it for
  timing. Where a unit's latency VALUE comes from is decision B-1
  (approved: ship the cvar live for every unit now; the EDU mapping lands
  when the owner picks the key).
- **F3** The order surface today: `FUnitOrder` = Move / Stop / Garrison /
  ExitGarrison (`Public/BattleTypes.h`, the decision-A2 comment block);
  player orders enter via the exec/demo surface; auto-orders use the RC7
  pattern (`UUnitRegistrySubsystem::ApplyUnitSkirmishDecision`) with the
  order-serial guard. The landed auto-issuers: rout flee (MC5), rally Stop
  (MC6), impetuous auto-charge (MC9). The queue must define its relation to
  the serial guard (R-C1; B-4's serial-at-issue is the answer).
- **F4** The transient-bookkeeping precedent: `bRetreating` /
  `AutoRetreatSerial` (`Public/BattleTypes.h`) plus the spec §3.5 transient
  list (tick stamp, flee bookkeeping, team general records -- never saved,
  rebuilt). The queue rides this posture (B-3 approved).
- **F5** The GeneralAura shape TerrorAura mirrors: per-unit bonus computed
  in the morale pass's geometric fold at the cluster cadence
  (`Private/MoraleTickProcessor.cpp:610-613`,
  `Base * (1 - DistM / Radius)` vs the living general anchor from the
  registry's per-team record); cvars `Battle.Morale.GeneralAuraRadiusM` /
  `GeneralAuraBase` (`Private/BattleMorale.cpp:128-137`); weight row
  `FMoraleWeights::GeneralAura`; the same input gates rally eligibility
  (`Private/MoraleTickProcessor.cpp:361`). *(Re-verified; the fold line
  anchors moved 610->610-613 with the 4a close's edits.)*
- **F6** The EDU `attributes` field is arity 0..-1, EduToken, optional
  (`Private/BattleEduParser.cpp:9,35+`); unknown tokens loud-fail (the B-3
  posture). `frighten_foot` / `frighten_mounted` / `hardy` / `very_hardy`
  join FromEdu's TOKEN MAP -- the schema table itself is untouched unless
  `stat_heat` becomes a key (then the officer-key amendment precedent:
  occurrence-cap amendment + golden re-sign 16 -> 17). *(Re-verified.)*
- **F7** `FatigueRateScale` is live but unwired: the seam parameter defaults
  to `1.f` (`Public/BattleMorale.h:169`); the SOLE call site passes four
  args (`Private/MoraleTickProcessor.cpp`, the exertion loop's
  `AccumulateExertion(State->ExertionIntegral, TickDt, bRunning,
  bFighting)`); the clamp `>= 0` already guards it
  (`Private/BattleMorale.cpp:216-222`). Wiring it = EDU tokens ->
  attrs/registry -> the call site. The T4.2 band/multiplier golden is NOT
  touched (rates change trajectories, not bands -- R-C5). *(Re-verified.)*
- **F8** No 4b gate rows exist in the roadmap T4 table; this brief proposes
  its own T4b IDs and records them via a B.9-style amendment row (Task 4).
  The inherited tripwires: `DeterminismTwoRun` (GREEN, PASS 40/40 with the
  full morale loop), `Battle.Unit.StateHash.CensusGuard`, `Det2RunCensus` /
  `Det2RunSec` telemetry, the T4.8 staleness probe, the cadence watch (the
  B.7 entry -- morale-cadence changes re-run the goldens + the two-run).
- **F9** Census discipline: unit-level only (the A3 ruling extended) ->
  no new per-entity fragments or tags -> `CensusBits<Ts...>` unchanged
  (the 20-type list, `BattleStateHash.cpp`), the archetype count expected
  constant (assert it in the DoD); the census list grows ONLY if a
  composition-visible type appears.
- **F10** The reset-ordering rule (BINDING, the 2026-11-02 root cause; the
  B.7 entry carries it): every new accumulator is reset-ordering-immune --
  `ResetBattleState` zeroes registry state mid-frame, and an accumulator
  that re-arms on the emptied world shifts phase between runs (the hunt
  record: `Docs/phases/phase-4/task1-hunt-archetype-census.md` §9). The
  delay queue's countdown is the next candidate for exactly this disease.
- **F11** Baselines at the kickoff HEAD: unit 85/85; func 26/27 = exactly
  S5_Mob (its own hunt, carried per the 4a report §5); Jenkins
  `massbattle-gate` green; **the full staged nightly `[nightly] DONE`
  (2026-11-03)** -- the reference carries the 4 new soak fields, the
  morale-on soak dynamics (kills + the death-variant archetype family) are
  the nightly's normal state, the archetype gate is spawn + 4 (not
  equality), and the soak's `dressErr` is advisory (<= 40) with the gate
  binding on `settledDressErr <= 6`. *(SLOT resolved from the report §5.)*
- **F12** The pr15.3 lesson inverted: the two-run base is GREEN -- the 4b
  extension (B-8) is the first clean-base extension since phase 3.

## 2. Pointers

MCP/editor automation: operating rules live in `Docs/reference/mcp-playbook.md`
(its §8 correction ledger first). **SLOT RESOLVED at freeze: MCP-light like
4a** -- all fixtures C++/ExecBridge-authored, evidence via `run_tests.bat`
+ the staged nightly; the 4a report's residual list is owner-scoped (the
PIE pass, keybinds, per-map assets, the arrow-mass screenshot) and not 4b
run items. No editor-side items anticipated.

## 3. Phase ground rules + owner decisions + risk watchlist

### 3.1 Owner-decision table (ALL ROWS APPROVED by the owner 2026-11-03)

| ID | Decision | Choice |
|----|----------|--------|
| B-1 | The latency VALUE's source | EDU-mapped per row (which key: owner call) overriding a global default cvar `Battle.Order.LatencyTicks` (the tuning home); 4b ships the cvar live for every unit and the EDU mapping when the key is chosen |
| B-2 | Which orders delay | Player-issued orders only (Move / Stop / Garrison / ExitGarrison / stance changes via the exec surface). Auto-issued orders NEVER delay: rout flee, rally Stop, impetuous charge, skirmish retreat are sim reflexes -- delaying them breaks MC5/MC6/MC9 semantics |
| B-3 | Queue residency | Transient bookkeeping on `FUnitRuntimeState` (the F4 posture): unsaved, cleared on capture, absent on restore. A save schema v4 is NOT opened for pending orders |
| B-4 | Release semantics | The serial bumps at ISSUE (not release); the queued order carries issue tick + serial; release on tick boundaries in sorted-handle order; a second player order during the delay REPLACES the queued one (last order wins, the M2TW posture) |
| B-5 | TerrorAura behavior surface | Morale-drain term only (a weight row mirroring GeneralAura against enemy envelopes), isolated at a known weight (the T4.1 golden pattern). Fright-induced fleeing/routing beyond the drain stays T6.1 tuning, not 4b |
| B-6 | TerrorAura input mechanics | The existing 4 Hz cluster-cadence fold; the anchor is the nearest frightener unit's envelope; falloff `Base * (1 - DistM / Radius)` (the F5 shape); `frighten_foot` vs `frighten_mounted` map to per-token Base/Radius via the cvar block |
| B-7 | The climate slice | `hardy` -> 0.85, `very_hardy` -> 0.7 `FatigueRateScale` (A16 placeholders, golden-locked); `stat_heat` DEFERRED to Phase 5 with the weather/terrain system it feeds -- the slot decision recorded in §10's table row |
| B-8 | Gates | The two-run gate EXTENDS to 4b inputs (queued releases + terror terms + rate scales) under the DeterministicTick pin -- F12; new unit suites `Battle.Unit.Order.*` + `Battle.Unit.Morale.{Terror,RateScale}`; T4.8 staleness re-asserted; T4b rows recorded via a B.9 amendment row (authorized in Task 4) |

### 3.2 Phase ground rules (additions to AGENTS.md only)

1. Unit-level only (A3 extended): zero per-entity fragments, zero new GAS
   attributes (F1); the queue and any terror/rate state live on
   `FUnitRuntimeState` or the registry.
2. Every accumulator is reset-ordering-immune (F10) -- asserted by test, not
   by comment.
3. Every new cvar block defaults to the identity behavior (B.8 item 6):
   `Battle.Order.LatencyTicks=0` and the terror weight 0 => byte-identical
   hashes and soak FINAL lines (the F14 append-only rule for any new
   counter).
4. The order path allocates nothing per tick in the steady state (the EC9
   discipline; the queue is registry-resident scratch).
5. Spec docs are read-only (AGENTS §7): the only authorized spec touch is
   the roadmap B.9 amendment row (B-8) and the §10 extension-point table's
   two rows being marked landed -- both disclosed in the report.

### 3.3 Risk watchlist

| ID | Risk | Mitigation / watch |
|----|------|--------------------|
| R-C1 | The delay queue vs the RC7 serial guard: an auto-order sneaks in during a player order's delay, or a delayed order resurrects a cancelled intent | Serial bumps at ISSUE (B-4); the queue holds only the latest player order (last order wins); the ToggleBehaviors stale-bookkeeping lesson (pr13.5) is the regression shape -- assert the cancel path |
| R-C2 | The countdown accumulator catches the morale-cadence disease (reset mid-frame re-arms differently across runs) | F10: reset-ordering-immune by construction + a CensusGuard-style history-independence assertion in the new unit suite |
| R-C3 | TerrorAura perturbs the demo arc (the T6.1 re-clock made the cascade organic; fear on the enemy side may route E4/C2 before beat 5) | Default-off ships first; the demo enables it only in its session flags; re-verify the beat clocks + the ResolveProbe verdict with the flag on; the beat-5 wavering-friendly owner call (D.12) may interact |
| R-C4 | EDU token additions touch fixture rows -> golden re-signs | Loud-fail keeps unknown tokens rejected; only rows the fixtures actually carry re-sign; `Battle.Data.EduRoundTrip` ratifies |
| R-C5 | `FatigueRateScale` shifts exertion trajectories -> S7 / EffectEndToEnd timing margins and SoakRout counters move | The band/multiplier golden is untouched (F7); the affected timings re-measured with the flag on in Task 4; disclosed, not regoldened silently |
| R-C6 | Queue cost at 2,400-entity scale | The queue is per-UNIT and mostly empty; `OrderMs`-class metric rides DumpMetrics; no per-tick allocation (ground rule 4) |

## 4. API digest

Expected EMPTY -- no new engine territory (see the header row). If any task
meets an unrecorded engine symbol, audit it against the pin FIRST (the I6
pattern; results merge into `Docs/reference/engine-api-5.8.md` per AGENTS
§4) and record the verdict here before building on it.

SLOT: (deliberately empty at skeleton time -- carried: no engine symbol
surfaced during the freeze verification)

## 5. Task plan

Gate-first discipline as always: each gate test lands in its owning task's
commit. **SLOT RESOLVED: the tier expectations at freeze are F11's** --
unit green (85/85 + the new 4b suites), func 26/27 = only S5_Mob (its own
hunt, carried; NOT 4b scope).

### Task 0 -- Kickoff verification

**Goal.** The 4a state is measured and the phase is CI-visible before any
new work.

**Steps.** (1) Re-verify §1's facts against the kickoff HEAD (DONE at
freeze -- the F-ids re-cited above; re-verify again at the run's HEAD if
commits landed between the freeze and Task 0). (2) Run both tiers; expect
unit green, func failing ONLY S5_Mob (F11). (3) Confirm the H-4 patch's
applied state (DONE -- the roadmap B.4/B.5 carry the census wording).
(4) Create `Docs/phases/phase-4/progress-4b.md` (the phase-2 two-brief
precedent) with the session header.

**Acceptance check.** Tier counts recorded verbatim; the digest re-verified
line noted.
**Commit.** `phase4b: task 0 kickoff verified`

### Task 1 -- The OrderLatency queue (B-1..B-4)

**Goal.** A player order takes effect after the unit's latency elapses; the
serial-guard and auto-order semantics are untouched.

**Steps.** (1) The queue: issue tick + serial on `FUnitRuntimeState`
(transient, B-3); release on tick boundaries in sorted-handle order; the
release path calls the EXISTING order apply (no second apply implementation).
(2) The value: `Battle.Order.LatencyTicks` default 0 (identity); the EDU
mapping per B-1's chosen key (deferred -- the cvar ships live for every
unit). (3) The serial-at-issue + last-order-wins + cancel paths (R-C1) with
unit tests: `Battle.Unit.Order.Queue` (release timing, replacement, auto-order interplay) + the reset-ordering assertion
(R-C2). (4) Flags-off identity: latency 0 => byte-identical hash. (5) The
exec/demo surface rides it unchanged (the queue sits under the apply, not
beside it).

**Acceptance check.** Unit suite green incl. the history-independence case;
two-run green with latency > 0 under the pin; flags-off byte-identical.
**Commit.** `phase4b: task 1 the order-latency queue`

### Task 2 -- TerrorAura (B-5/B-6)

**Goal.** Frighteners drain enemy morale through the existing fold; the EDU
tokens join the parse.

**Steps.** (1) The enemy-anchor fold beside GeneralAura's (F5), the weight
row + `Battle.Morale.TerrorAura{RadiusM,Base}` + the per-token
foot/mounted split. (2) `frighten_foot`/`frighten_mounted` in FromEdu's
token map (F6); fixture rows only where the fixtures carry them (R-C4).
(3) Unit golden: `Battle.Unit.Morale.Terror` (the term isolated at known
weight; falloff shape; foot vs mounted). (4) The overlay ring (red, the
existing morale-row framework) + the `TerrorActive` metric (append-only).
(5) Flags-off identity re-asserted.

**Acceptance check.** Golden green; S7 + OfficerDeath unregressed with the
flag on; the demo boots with the flag session-on (R-C3 evidence).
**Commit.** `phase4b: task 2 terror aura + frighten tokens`

### Task 3 -- The climate data slice (B-7)

**Goal.** `hardy`/`very_hardy` reach the live-but-unwired rate slot; the
stat_heat decision is recorded.

**Steps.** (1) Tokens -> attrs/registry -> the `AccumulateExertion` call
site (F7's three anchors). (2) Unit golden: `Battle.Unit.Morale.RateScale`
(the rate values; the band table asserted UNCHANGED -- R-C5). (3) The
stat_heat deferral recorded in the spec §10 row (ground rule 5). (4) The
affected timing margins re-measured (S7, EffectEndToEnd) with the flag on.

**Acceptance check.** Golden green; the S7/Effect timings disclosed; the
spec row updated.
**Commit.** `phase4b: task 3 hardy rate scales + stat_heat slot decision`

### Task 4 -- Determinism + gates (B-8)

**Goal.** The two-run gate covers the 4b inputs; the T4b rows exist.

**Steps.** (1) `DeterminismTwoRun` extended: queued releases (latency > 0
in the fixture), terror terms, rate scales -- under the DeterministicTick
pin; isolated AND in-gate (F12: the first clean-base extension since
phase 3). (2) T4.8 staleness re-asserted (the queue adds no value-age).
(3) The T4b amendment row into the roadmap (B.9-style, authorized here).
(4) Soak: the FINAL line gains any new counter append-only; one nightly
evidence run. **SLOT RESOLVED: the staged-nightly stack state is F11's** --
the pipeline is green end to end (the 2026-11-03 `[nightly] DONE`); the
4b nightly evidence rides the same path (the reference now carries the
soak telemetry fields; the archetype budget is spawn + 4; the dress gate
is arrival-judged).

**Acceptance check.** Two-run PASS mismatches=0 both configurations; the
roadmap row landed; the nightly line recorded.
**Commit.** `phase4b: task 4 determinism + T4b gates`

### Task 5 -- B.8 closure, docs, final verification

**Goal.** Observable, measured, documented, regression-protected.

**Steps.** (1) B.8 rows 1-7 each with evidence (overlay: aura rings red +
queue peek; metrics: the new counters; B.7 checklist entry; CVAR flags).
(2) The ADR proposal for the queue posture (serial-at-issue, transient,
auto-orders never delay) handed to the owner. (3) `Docs/README.md` status +
map rows (the doc map's promised name confirmed). (4) The report per AGENTS
§5, its section 5 naming the Phase 5 entry points (representation; the
weather/terrain systems; the stat_heat slot).

**Acceptance check.** B.8 complete with evidence; CI green; report filed.
**Commit.** `phase4b: task 5 overlay + metrics + docs` then `phase4b: complete`

## 6. Gate -> implementation map

| Gate | Tier | Task | Test name | Asserts (short) |
|------|------|------|-----------|-----------------|
| T4b.1 | Unit | 1 | `Battle.Unit.Order.Queue` | release on tick boundary after the latency; serial-at-issue; last order wins; auto-orders undelayed; reset-ordering immunity |
| T4b.2 | Unit | 2 | `Battle.Unit.Morale.Terror` | the drain term isolated at its weight; falloff shape; foot/mounted split |
| T4b.3 | Unit | 3 | `Battle.Unit.Morale.RateScale` | hardy/very_hardy rate goldens; the band/multiplier table unchanged |
| T4b.4 | Func | 4 | `DeterminismTwoRun` (4b-extended) | two-run equality incl. queued releases + terror + rates under the pin |
| -- | Data | 2/3 | `Battle.Data.EduRoundTrip` (re-signed rows if touched) | the token additions round-trip; unknown tokens still loud-fail |

## 7. DoD checklist

- [ ] T4b.1-T4b.4 green with evidence; tiers green except S5_Mob (F11)
- [ ] Archetype count unchanged (the census list untouched; F9) across all scenarios and the soak
- [ ] Flags-off identity byte-identical (hashes + soak FINAL lines) for every new cvar
- [ ] `CensusGuard` semantics untouched; `Det2RunCensus`/`Det2RunSec` still green
- [ ] The reset-ordering assertion present for the queue (R-C2), per F10
- [ ] B.8 items 1-7 each checked with evidence (unit tests, scenario, overlay, metric, B.7 entry, CVAR flags, Iris invariants -- the queue is plain data, no raw pointers)
- [ ] The roadmap B.9 amendment row + the spec §10 rows landed (ground rule 5), disclosed
- [ ] `Docs/README.md` status + map updated in the introducing commit
- [ ] progress log complete; the report per AGENTS §5 filed; the ADR proposal handed to the owner

## 8. System prompt

```text
You are the MassBattle Phase 4b implementation agent.
Read Docs/README.md, then Docs/AGENTS.md, then
Docs/phases/phase-4/brief-command-and-fear.md.
Execute the brief autonomously under the standing protocol; brief §3.1
decisions are owner-approved. Stop at the brief's DoD (§7) after writing
the handoff report and the final commit.
```

---

## 9. The freeze record (2026-11-03)

1. The 4a run closed: `Docs/phases/phase-4/report.md` exists, §5 names the
   4b entry points. DONE.
2. H-4 applied: the roadmap B.4/B.5 carry the census wording. DONE.
3. B-1..B-8 approved: the owner's 2026-11-03 ruling, recorded in the
   README's status block; the rows above carry the approval inline. DONE.
4. §1 re-verified at `90c3fb1`: F1 exact (all six anchors unchanged); F5's
   fold re-cited (`MoraleTickProcessor.cpp:610-613`, the rally gate `:361`);
   F6/F7 re-verified; F11 rewritten from the 4a close's evidence (the
   nightly DONE, func 26/27). DONE.
5. SLOTs resolved: §2 (MCP-light, the 4a residuals are owner-scoped), Task 0
   (the tier expectations = F11), Task 4 (the nightly state = F11), the
   header companion docs (the report exists). §4's empty slot carried
   (no engine symbol surfaced).
6. The map row lands in the introducing commit. DONE (this commit).
