# MassBattle — Phase 0 Audit (plan & implementation)

> **Archive note (2026-10-25 reorg).** Phase 0 audit + fix-pass record. The §7 fix-status table is current as of 2026-10-24; the four deliberately deferred items are tracked in the status block of `Docs/README.md`.

**Date:** 2026-10-24
**Scope:** Full audit of the Phase 0 plan documents and implementation: `Docs/phases/phase-0/brief.md`, `Docs/phases/pre-phase-0/brief.md`, `Docs/spec/{architecture,roadmap-and-testing}.md`, `Docs/reviews/plan-review-findings.md`, all agent reports (`Docs/phases/*/`), all plugin source (`Plugins/MassBattle/Source/MassBattle/{Public,Private}`, Build.cs, uplugin), all test files, CI tooling (`Tools/CI/*`, `Jenkinsfile`), baselines, and configs.
**Method:** Every finding was checked against the code as committed at `3ba9ec3` ("phase0: complete"). Contested UE API claims were verified against the pinned 5.8.4 engine source at `E:\Unreal\UE_5.8\Engine\Source` (and `Engine/Plugins/Runtime/GameplayAbilities`) before being asserted here. File:line references are to the audited commit.

**Verdict:** The phase is substantially well-executed — the evidence protocol, the reality-wins API audit (`Docs/reference/engine-api-5.8.md`), and the ADR-15 on-machine probe are exemplary. Several patterns that looked suspicious were verified against engine source and are **correct** (§4 below). However, the audit found **3 high-severity issues** that the green gates mask, plus a systemic pattern of contract-to-test drift (§2) where assertions shipped weaker than the roadmap specified, without a logged deviation.

| Severity | Count | Items |
|----------|-------|-------|
| High | 3 | H1 unregistered death tag · H2 vacuous nightly gate · H3 multi-kill double-signal |
| Medium | 8 | M1–M8 |
| Low / nit | ~15 | §3 |

---

## 1. High severity

### H1. `Battle.Unit.Died` is never registered as a gameplay tag — the GAS up-path works by accident

The tag is requested with `ErrorIfNotFound=false` in `BattleDeathPath.cpp:19` and `UnitActor.cpp:127`. There is no `Config/DefaultGameplayTags.ini` and no native tag registration anywhere in the repo, so `EventTag` is an **invalid** tag on send, and the listener's filter container is **empty**.

Engine-verified semantics (`AbilitySystemComponent_Abilities.cpp:2598`):

```cpp
if (SearchPair.Key.IsEmpty() || EventTag.MatchesAny(SearchPair.Key))
    SearchPair.Value.Broadcast(EventTag, Payload);
```

An empty filter container matches **every** gameplay event. Consequences:

- `AUnitActor::DeathEventCount` counts *any* GAS event on the actor — a future "Battle.Unit.Charged" event silently corrupts T0.5-style asserts.
- Any Phase 4 consumer that legitimately listens for the named tag will never fire.
- The public contract "death raises `Battle.Unit.Died`" does not actually exist as a named tag.

**Fix:** register the tag (`UE_DEFINE_GAMEPLAY_TAG` natively or `Config/DefaultGameplayTags.ini`), `checkf(Tag.IsValid())` at the send site, and add a unit test asserting `RequestGameplayTag("Battle.Unit.Died").IsValid()`.

### H2. The nightly perf gate is vacuous — it compares the baseline file to itself

`Tools/CI/nightly.bat` step 4 runs `write_baseline.py`, which **overwrites** `Baselines/perf/ref-i7-9700F-1660Ti/nightly-smoke.json` in place with tonight's value, then runs:

```
compare_baseline.py --baseline <nightly-smoke.json> --candidate <nightly-smoke.json>
```

Same file → always PASS. There is no stored history and no previous-generation reference, so R8's "compare fails on regression" is not implemented. Additionally, `nightly.bat` never checks `compare_baseline.py`'s exit code (moot today given the self-compare, a bug once the compare becomes real).

This matters because the loop must be trustworthy *before* the real perf gates arrive (T2.9), and R8 was the owner's #1 priority directive for this phase.

**Fix:** treat the committed file as the immutable reference; write tonight's run to a candidate file; compare reference vs candidate; commit the reference forward only on PASS (or as an explicitly signed new baseline). Check the compare exit code in `nightly.bat`.

### H3. The death observer re-signals all dead entities on every flush — multi-kill frames double-fire GAS events and double-queue destroys

`UBattleDeathObserver::Execute` (`BattleDeathPath.cpp:58-81`) runs a query over *all currently `FDeadTag`-tagged entities* on each observer invocation (the 5.8 notification carries no entities — correctly documented). Timeline for two kills in one frame:

1. Kill A flushes → observer fires → signals A (deferred signal).
2. Kill B flushes → observer fires → queries all dead: **A and B** (A is still tagged — its destroy is deferred to the receiver later in the frame) → **A is signaled again**.
3. `UBattleDeathSignalReceiver` runs twice for A: `RemoveSingle` returns 0 on the second call (harmless), but `SendGameplayEventToActor` fires again (GAS event ×2) and `Context.Defer().DestroyEntity(Entity)` is queued **twice** on the same entity.

Every Phase 0 test kills exactly one entity, so this is invisible today — and mass-death frames are precisely the Phase 2 workload (the R1 concern ADR-15 defers). It also makes the observer's cost O(all dead) rather than O(newly dead).

**Fix options:** a second `FDeadSignaledTag` swap on signal, a registry-side set of signaled-but-not-destroyed entities consumed by the receiver, or perform the destroy in the observer's own flush and signal from there. Add a functional assertion with two kills in one frame (GAS count == 2, destroy count == 2, roster size == N−2).

---

## 2. Medium severity

### M1. T0.5's contracted assertion — "removed from live queries" — is never run as a query

The roadmap gate table and `Docs/reference/engine-api-5.8.md` §1/§3 specify a query with `AddTagRequirement<FDeadTag>(EMassFragmentPresence::None)` counting 0. Implemented instead: `HasTag<FDeadTag>()` on the entity (`BattleFuncTestActor.cpp:347`), and in `S0_ExitCriteria` the check `bTagged = !bDeadValid || HasTag(...)` (`:518`) passes **vacuously** once the entity is destroyed. An archetype flip that somehow kept the entity in live queries would not be caught. Add the presence-None query count as the primary assert.

### M2. T0.9 is largely tautological, and weaker than the project's own audit promised

`AssertTestMode` (`BattleTestSettings.cpp:59-106`) reads genuinely active engine state only for fixed step (`FApp::UseFixedTimeStep()` + delta). The time-slicing and full-detail checks read only the project's own `Battle.Test.*` cvars — which `ApplyTestMode()` just set — so "violation detected" reduces to "someone turned the flag off." Audit §11 promised a runtime query probe asserting no variable-tick chunk fragment / no MassLOD trait on battle archetypes; it does not exist anywhere. A few lines of Phase 1 code adding a MassLOD trait would sail through the contract. Implement the promised query probe and read engine-side state (`mass.*` cvars, archetype composition) for the other two checks.

### M3. String `TestEqual` is case-insensitive (`Stricmp`) — the EDU canonical golden is blind to case, and one test expectation is wrong underneath

Engine-verified (`AutomationTest.cpp:2161-2163`): `FAutomationTestBase::TestEqual` for `TCHAR*`/`FString` compares with `FCString::Stricmp`; case-sensitive comparison requires `TestEqualSensitive`. Consequences:

- `MassBattleEduTests.cpp:216-219` expects `soldier x` / `dictionary x` (lowercased) while the parser's `EduName` kind is deliberately verbatim (`BattleEduParser.h` canonicalization rule 4) and the round-trip golden correctly preserves `soldier Peasant` (`:105`). The edge-case test encodes a rule the parser does not implement, and the case-insensitive compare hides the contradiction — both tests pass, but they cannot both be describing the parser.
- The T0.2 golden comparison (`:130`) would pass even if the canonicalizer's case handling regressed.

**Fix:** use `TestEqualSensitive` for all canonical-form comparisons and correct the edge-case expectation to `soldier X` / `dictionary X` (it will then fail against the current code — which is the point).

### M4. State hash includes transient scheduling state

`ComputeBookkeepingHash` (`BattleRegistry.cpp:224`) folds the per-unit dirty bit, and `FBattleStateHash::Compute` uses it as term 1 — while `BattleStateHash.h` claims "no wall clock, no frame counters, no non-sim state." A `MarkStatsDirty` with no data change alters the hash. In Phase 2's per-tick two-run hash equality (T2.8), any pipeline-timing asymmetry between runs produces false divergence. Exclude `DirtyStats` from the hash (keep roster sizes and serials).

### M5. Roster indices go stale on death

`HandleUnitDeath` uses `Roster.RemoveSingle(Entity)` (`BattleRegistry.cpp:135`), which shifts surviving entries without restamping their `FOwnerUnitFragment.RosterIndex`. Arch §2.4 calls the roster the authoritative bookkeeping; Phase 1 slot assignment reads roster indices. Today nothing reads the fragment's index after a death (T0.5 kills index 5 of 8 and asserts only the size), so it is latent. Fix while cheap: document the contract and restamp (or swap-remove with an index remap) on death.

### M6. Hot-path logging contradicts the project's own B.7 gate claim

`UnitSyncProcessor.cpp:65` logs one `UE_LOG(Display, ...)` line **per chunk per frame** (before the dirty early-out), plus `ChunksMatched` per Execute (`:96`); the death observer logs per entity (`BattleDeathPath.cpp:67,148`). At Phase 2 scale that is thousands of formatted lines per second — and the B.7 Phase 0 entry claims "no allocations added in UnitSync/DeathObserver hot paths." These are Task 8/9 diagnostics that were never demoted. Gate behind `VeryVerbose` or delete.

### M7. `Battle.StateHash` console command still emits the placeholder, not the real hash

`BattleExecCommands.cpp:75` emits `ComputeBookkeepingHash()` behind the comment "Task 10 replaces the placeholder with the full CityHash64 sim-state hash" — Task 10 shipped `FBattleStateHash::Compute` but never rewired the command. The brief (Task 10) explicitly says the command emits it; the documented operator/evidence interface reports the wrong (32-bit, dirty-bit-polluted) value. One-line fix plus updating the help text.

### M8. Route B (config/template spawn) is structurally incompatible with ADR-14, and nothing records that

`UBattleUnitTrait::BuildTemplate` (`BattleUnitTrait.cpp`) bakes a shared fragment with a **null** `OwnerUnit` anchor into the template; the comment says "the spawn path replaces it with the real per-unit instance" — but nothing replaces it, and arch §3.3 **forbids** rebinding shared instances at runtime. Spawning two units from the same config gives both the same null-anchored instance — the exact E1 cross-contamination hazard ADR-14 exists to prevent. Phase 0 only config-spawns 3 entities from one config in T0.3, so it is latent, but the architecture presents Route B as *the* spawn method (§5.3) while ADR-14 makes it unusable for real units. Record an ADR (Route A is the unit-spawn path; Route B is config-integrity validation only) and add a test asserting config-spawned entities are not used as units, or implement a per-unit payload mechanism for templates.

---

## 3. Low severity / nits

- **Stale comments contradicting ADR-15's final state** (regular tag): `BattleFragments.h:4` ("FDeadTag is a 5.8 sparse tag per ADR-15" — directly above the regular-tag struct), `BattleExecCommands.cpp:237`, `BattleFuncTestActor.h:63`. Also `UnitActor.h:66` claims delegates bind in `PostInitializeComponents` while `UnitActor.cpp:28-31` binds in `SpawnEntities`; `BattleRegistry.h:19` / `BattleTypes.h:59` say "Serial++ on every free" while the code increments on every *alloc* (`BattleRegistry.cpp:54` — behaviorally equivalent, doc wrong).
- `Battle.RngSelfTest` command (brief Task 10 step 2) was never implemented and not logged as a deviation.
- **FApp fixed-step leaks from a unit test into the editor session**: `FTestMode_AssertAndViolation` (`MassBattleStateHashRngTests.cpp:202`) calls `ApplyTestMode()` and never turns fixed step back off at test end — the editor context that ran the unit tier is left ticking fixed-step 1/60.
- `bBow` is never derived from the EDU row (`BattleSpawnLibrary.cpp:53`, `bMelee = !bBow` is always true) — documented placeholder. `FBattleRowStats::FromEdu` also re-parses with `FCString::Atof/Atoi` (unvalidated) rather than reusing the parser's already-validated normalized values.
- Receiver finds unit actors via `TActorIterator` linear scan per dead entity (`BattleDeathPath.cpp:136`) — bypasses arch §2.4 ("communicate only through the registry"); the registry should own a handle→actor map by Phase 2.
- `SetAttr` matches actors by handle `Index` ignoring `Serial` (`BattleExecCommands.cpp:215`).
- The RNG "persistence" check only compares `GetCurrentSeed()` (`MassBattleStateHashRngTests.cpp:186`), which a freshly re-seeded stream would also pass; verify continuation (draw values) instead.
- `AttributeSet.ClosedSet`'s "ADR-09 mod-target" assertion is circular — it checks that the literal `"MeleeAttackMod"` ends with `"Mod"` (`MassBattleAttributeSetTests.cpp:46`), proving nothing.
- `FUnitRuntimeState.bInUse` is written but never read; `CreateArchetype` failure leaks the already-registered unit slot (`BattleSpawnLibrary.cpp:114-118`); destroying an `AUnitActor` never releases its registry slot or entities (no teardown path — fine in Phase 0, a leak in any real map).
- EDU strictness nits: `EduToken` accepts internal whitespace (`"inf anty"` passes as one token); `CommitUnit` order errors report `LineNumber=0` (`BattleEduParser.cpp:240`); `-0` survives int normalization; floats with >2 decimals are silently rounded rather than rejected (documented rule, but the "strict" posture suggests rejecting).
- `MassBattle.Build.cs` links `FunctionalTesting` (a Developer module) and `Gauntlet` unconditionally in a Runtime module — a Shipping game target will fail UBT. Gate with `if (Target.bBuildDeveloperTools)` before anyone tries a Shipping build. (The gating rationale in `BattleSmokeController.h` addresses the runtime path, not the link dependency.)
- `.uproject` ships with ~25 MCP/editor toolset plugins enabled and no `TargetAllowList` — cook bloat and a recurring packaging risk (the GameFeatureData cook failure the nightly hit was a symptom). Add `"TargetAllowList": ["Editor"]` to the toolset entries.
- `L_MassBattle_T0_Functional` is opened by name (`OpenLevel`) and referenced by no asset — fine for dev-tier runs from source, but it will not exist in a cooked build unless added to the cook's map list; the func tier silently breaks the day it runs on a staged build.
- Registry `HandleUnitDeath` takes `RosterIndex` purely for logging; either drop the parameter or use it to validate the removal.

---

## 4. Verified-correct items (do not "fix" these)

Checked against the pinned engine source; recorded here so a future reader does not re-litigate them:

1. **UnitSync's one-shared-fragment-per-chunk pattern is sound.** 5.8 chunks are shared-fragment-homogeneous: `FMassArchetypeData::GetOrAddChunk` only merges entities whose `FMassArchetypeSharedFragmentValues` are equivalent (`MassArchetypeData.cpp:510`), and `ExecuteFunction` binds shared fragment views per distinct chunk value (`:818-827`). Per-unit ADR-14 anchors therefore guarantee per-unit chunks, and `Context.GetMutableSharedFragment<T>()` per chunk is the correct idiom — including after death archetype flips (the live chunk keeps its shared values).
2. **The ADR-14 anchor genuinely defeats content-CRC dedup.** `Battle.Unit.Mass.SharedFragUniqueness` proves it both directions (anchor-less payloads dedup to one instance — the E1 hazard demonstrated, not just asserted).
3. **The functional harness cannot silently pass.** `AFunctionalTest::AddError` only logs (verified: `FunctionalTest.cpp:1290-1293`); all three S0 actors track their own `bOk`/assertions and set `Result` via `CompleteTest`; the harness (`MassBattleFuncTestHarness.cpp:167-171`) re-checks `Result` and owns a timeout. `TimeLimit` also works for the two actors that skip `Super::StartTest()`, because `TotalTime += DeltaSeconds` runs in the base Tick regardless (`FunctionalTest.cpp:418`).
4. **The ADR-15 sparse-tag revert is legitimate and well-evidenced.** The brief's "do not reverse" (§3.1 A3) was correctly overridden by ground rule 4 (reality wins); the probe's quoted engine comments match the source (`MassObserverRegistry.cpp:25`, `MassSparseElementsStorage.h`), and the roadmap/architecture annotations were patched coherently.
5. **Registry T0.1 tests are real:** the 10k round-trip verifies the unit count returns to zero and the free list holds every slot exactly once; stale handles are rejected through every lookup path.
6. **`summarize_run.py` is trustworthy:** it refuses to pass without the `**** TEST COMPLETE ****` marker, treats any non-`Success` state as failure, cross-checks the `-ReportExportPath` counts, and exits nonzero on mismatch.
7. **`BattleFragments.h` hot-fragment hygiene** is clean (no `FString`/`TArray`/`UObject*`), and the registry holds plain structs only, per arch §3.1.

---

## 5. Plan-level observations

1. **Contract-to-test drift is the systemic pattern.** Nearly every medium finding is a case where the roadmap or audit promised an assertion shape (live-query count, active-settings read, per-command emit, `Battle.RngSelfTest`, variable-tick probe) and the implementation shipped a weaker proxy without logging a deviation — which the run's own protocol (evidence-before-claim, deviations logged) treats as a first-class event. A closing "contract vs. implemented assertion" pass over T0.1–T0.9 would have caught H1, M1, M2, M3, M7.
2. **DoD checkboxes are slightly overclaimed.** B.8 item 6 ("a CVAR feature flag" per capability) and B.7's "spawn/sync/death paths each carry their `Battle.*` cvar hooks": the only cvars that exist are `Battle.Test.*` and `Battle.Debug.*`; there are no per-path feature flags. `Docs/phases/phase-0/report.md` marks these ✓.
3. **One-commit-per-task** was violated once (Tasks 6+7 share `958c06d`) — disclosed in the report with a file-coupling rationale. Acceptable.
4. **The brief's field-count correction (13 → 14)** and the audit's C1–C12 corrections are consistent across brief, audit, and code — the reality-wins discipline held.

---

## 6. Recommended fix order

1. **H1 + M7** (one commit — tag registration touches both) + validity test.
2. **H2** (baseline reference/candidate semantics + exit-code check) — before any more nightly runs overwrite the reference.
3. **H3 + M5** (death-path idempotence + roster restamp; same area, one commit) + multi-kill functional assert.
4. **M1, M2** (assertion-shape upgrades: live-query count; active-settings + variable-tick probe).
5. **M3** (`TestEqualSensitive` sweep + fix the edge-case expectation).
6. **M4, M6, M8** (hash purity; log demotion; Route-B ADR).
7. Comment sweep + low-severity nits opportunistically.

---

## 7. Fix pass — status (2026-10-24)

The fix pass was executed against the audited commit. Step-by-step log with per-step verification: `Docs/phases/phase-0/progress.md`, "Audit fix pass" section. Verification totals: **UBT `MassBattleEditor` and `MassBattle` Win64 both `Result: Succeeded`; unit tier 16/16 green (was 15 — adds `Battle.Unit.DeathPath.TagRegistered`); func tier 3/3 green** with the new death-path evidence verbatim in `Tools/CI/last_run/func.log` (exactly two `DeathSignal` lines for two same-frame kills, `Kill flushed dead=2 live=6 marker=2`, `Tick=1 Roster=6 GAS=2`, `Destroyed=2 Restamp=ok`); the baseline compare tooling was exercised on a synthetic candidate (PASS, exit 0) and a synthesized regression (FAIL, exit 1).

| Item | Status | Fix |
|------|--------|-----|
| H1 | **FIXED** | `Battle.Unit.Died` registered as a **native** gameplay tag (`BattleGameplayTags.h/.cpp`, `UE_DEFINE_GAMEPLAY_TAG_COMMENT`; ini tags are editor-only and would not fix -game/packaged builds). Sender, receiver, and the `AUnitActor` filter rewired to the native tag with `ensureMsgf(IsValid())` guards. New unit test `Battle.Unit.DeathPath.TagRegistered` asserts the tag resolves from the registry tree. |
| H2 | **FIXED** | `nightly.bat` now writes a candidate JSON under untracked `last_nightly/`, compares against the committed reference with a **checked exit code**, promotes + commits only on PASS, fails on regression leaving the reference untouched (bootstraps when no reference exists). `write_baseline.py` gained `--out`/`--seed-from` so unmeasured metrics survive promotion. README documents the flow. Nominal PASS / synthesized-regression FAIL both verified. |
| H3 | **FIXED** | New `FDeadSignaledTag` (regular tag) emitted by the observer immediately after each signal; the observer's query requires it `None`, so a later same-frame kill flush cannot re-signal dead-but-undestroyed corpses. Regression-tested by `S0_DeathPath`, which now kills **two** entities in one frame and asserts exactly 2 GAS events, 2 destroys, roster 8→6. |
| M1 | **FIXED** | `S0_DeathPath` runs the contracted live-query counts as actual standalone queries (`FDeadTag All` == 2; `FOwnerUnitFragment` + `FDeadTag None` == 6) immediately after the audited flush, and asserts the tag on the corpses **while they are still alive** — the vacuous `!bDeadValid || HasTag` path is gone from `S0_ExitCriteria` too (tag asserted right post-flush). |
| M2 | **FIXED** | `AssertTestMode` gained a `UWorld*` parameter and now probes active engine state: `mass.SimulationTickingEnabled != 0`, `mass.AllowQueryParallelFor != 0` (audited scheduling state pinned), and a variable-tick query asserting no entity carries `FMassVariableTickChunkFragment` (the probe audit §11 promised; `MassLOD` added to Build.cs). The S-scenario harness passes the game world; the T0.9 unit test spawns real units before asserting so the probe is not vacuous. |
| M3 | **FIXED** | All canonical-form comparisons in the EDU tests switched to `TestEqualSensitive` (plain `TestEqual` is `Stricmp` and case-blind). The messy-input expectation corrected to **verbatim** EduName values (`dictionary X`, `soldier X`) — the old lowercased expectation contradicted canonicalization rule 4, and the case-insensitive compare had been hiding the contradiction. |
| M4 | **FIXED** | `ComputeBookkeepingHash` no longer folds dirty flags (transient pipeline state, not sim state). Header/inline comments updated. |
| M5 | **FIXED** | The death receiver defers an `FMassDeferredCreateCommand` restamp lambda (runs at the receiver's flush) that rewrites `FOwnerUnitFragment.RosterIndex` to live roster positions after `RemoveSingle` compaction. `S0_DeathPath` asserts all survivor indices post-destroy (`Restamp=ok`). |
| M6 | **FIXED** | UnitSync per-chunk + `ChunksMatched` logs demoted to `VeryVerbose`, `ExecuteCalls` to `Verbose`. Per-death lines stay `Display` deliberately: bounded by death rate and load-bearing for T0.5 evidence. |
| M7 | **FIXED** | `Battle.StateHash` console command emits `FBattleStateHash::Compute` (`%016llx`) with updated help text. |
| M8 | **FIXED** | ADR-17 recorded in the architecture decision log: Route A is the only unit-spawn path, Route B is config-integrity validation only. The `BattleUnitTrait.cpp` comment no longer claims "the spawn path replaces it" (nothing did). |
| Lows | **FIXED (most)** | Stale ADR-15/"sparse" comments corrected (`BattleFragments.h`, `BattleExecCommands.cpp`, `BattleFuncTestActor.h`); `UnitActor.h` now matches the actual delegate-binding site; registry Serial comments corrected ("(re)allocation", not "free"); `bInUse` dead field removed; archetype-creation failure unregisters the leaked slot; `AUnitActor::EndPlay` tears down roster entities (deferred destroy + flush, guarded on `IsProcessing()` and subsystem liveness) and unregisters the unit; `SetAttr` prefers registry-valid handles; EDU parser rejects internal whitespace in tokens, reports unit-start line on order errors, normalizes `-0` → `0`; RNG persistence check strengthened to a sequence-continuation assert (its first version had an off-by-two and **correctly failed** the first run — evidence the strengthened check has teeth); `Battle.RngSelfTest` command implemented (was missing vs. brief Task 10). |
| Deferred | **DOCUMENTED** | Shipping split of the `FunctionalTesting`/`Gauntlet` dependencies into a Developer-type test module (UHT forbids `UCLASS` inside `#if` blocks; needs a second module — Phase 1 work). `.uproject` `TargetAllowList` pruning of the MCP toolset plugins (owner's live MCP setup — owner's call). EDU floats with >2 decimals remain silently rounded (documented canonicalizer rule; rejecting would be a contract change). `L_MassBattle_T0_Functional` will need a cook-list entry before the func tier ever runs on a staged build. |

One incidental finding during the fix pass, worth keeping: the deprecation warning `MassEntityHandle.h has moved to MassCore (Mass/EntityHandle.h)` fires on every compile. Harmless today (MassCore is already linked per audit C12), but the includes in `BattleTypes.h`/`BattleFuncTestActor.h` should move to `"Mass/EntityHandle.h"` before the next engine release where the shim is removed.
