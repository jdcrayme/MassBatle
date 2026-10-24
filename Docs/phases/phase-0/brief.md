# MassBattle — Phase 0 BRIEF (v2.0 — owner-approved, ready to execute)

> **Archive note (2026-10-25 reorg).** Preserved record of the Phase 0 run — executed as written; do not execute again. During the reorg: §4 (API digest) and Part III (sources) were extracted to `Docs/reference/engine-api-5.8.md` (Parts B/C); §2 (MCP manual) was consolidated into `Docs/reference/mcp-playbook.md`; the standing protocol from §0/§3/§8 was generalized into `Docs/AGENTS.md`. §-numbering kept for citation compatibility with the progress log and report.

| | |
|---|---|
| **Companion to** | `Docs/spec/architecture.md` (arch), `Docs/spec/roadmap-and-testing.md` (roadmap), `Docs/reviews/plan-review-findings.md` (findings), `Docs/phases/pre-phase-0/report.md` (pre-phase-0 handoff) |
| **Engine pin** | UE 5.8.4 (CL 58902052) at `E:\Unreal\UE_5.8` — project `E:\Unreal Projects\MassBattle\MassBattle.uproject` |
| **Run mode** | Fully autonomous, single run (same protocol as pre-phase-0): progress log, one commit per task, reality-wins, stop at DoD |
| **Priority directive** | **Robust automation infrastructure (R8 CI) is the highest priority of this phase** — it is Task 1 and everything else lands inside it |
| **Status** | **FINAL** — owner approved A1–A7 on 2026-10-24; all approved items are merged into Part I and Part II is now the decision log. Hand this file to the executing agent as-is. |

**How this document is organized**

- **Part I** is the operational brief and is self-consistent: full Phase 0 scope (§5 task plan) with the owner-approved adjustments of §3.1 already folded in, the pre-researched API digest (§4) so the agent verifies symbols instead of discovering them, and the agent system prompt (§8).
- **Part II** is the decision log: the owner reviewed A1–A8 on 2026-10-24 — A1–A4, A6, A7 **approved**; A5 **delegated to preparer judgment (adopted)**; A8 **not requested**. Every approved item is already merged into Part I; Part II is kept for rationale only.
- **Part III** lists research sources with links.

---

# PART I — BRIEF

## §0. Mission, read order, token discipline

**Mission.** Build the Phase 0 data spine end-to-end per roadmap "Phase 0 — Foundation": registry, fragments/tags, EDU parse, batched spawn + fixup, GAS attribute stubs with the UnitSync down-path, the death signal up-path, the debug-overlay skeleton, T0.1–T0.9 green, **and the R8 CI infrastructure as the phase's first deliverable** (per-commit gate, nightly packaged-build Gauntlet orchestration, versioned baseline-JSON storage, reference-machine recording).

**Read order (exact — do not read more than this before coding):**

1. This brief: §1–§4 fully, then each task section of §5 just before executing it.
2. `Docs/phases/pre-phase-0/report.md`: **§2** (corrections table — these facts are authoritative), **§4** (MCP tool recipes — do not rediscover), **§8** (starting points).
3. `Docs/reference/mcp-playbook.md` (skim the relevant-toolset table; re-verify after any editor restart).
4. Architecture doc: §2 (domain model), §3 (fragments/tags/shared), §4 (pipeline), §5 (unit↔entity interaction), §10.1 (attribute set), §12 (budgets/tunables).
5. Roadmap: "Phase 0 — Foundation" + T0 gate table; B.0 (tooling + test-mode contract); B.8 (definition of done).
6. Findings file: blocks **E1, E4, E5** only.

**Token discipline rules.**

- Never re-derive a fact that §1 of this brief already states. Never re-research an API that §4 already states — cite the digest entry (e.g. `§4.3`) in your progress log instead.
- If reality contradicts this brief, **reality wins**: fix the brief's fact in place (edit the line), log the discrepancy in `Docs/phases/phase-0/progress.md`, and mark the affected §4 entry in the audit doc (Task 2).
- Code skeletons in §4 are **starting points with verify markers** — see the protocol in §4.0. You verify once (Task 2), then trust.

## §1. Verified environment facts (trust these over anything else)

Distilled from `Docs/phases/pre-phase-0/report.md` §2 + verified public sources. All rows marked **[machine]** were verified on this machine at pre-phase-0.

| # | Fact |
|---|------|
| 1.1 | **[machine]** Engine: UE 5.8.4, CL 58902052, branch `++UE5+Release-5.8`, at `E:\Unreal\UE_5.8`. Project: `E:\Unreal Projects\MassBattle\MassBattle.uproject`. OS: Windows 10 Home 19045. |
| 1.2 | **[machine]** Game module is **`MassBattleGame`** (`Source/MassBattleGame/`); the plugin module is **`MassBattle`** (`Plugins/MassBattle/Source/MassBattle/`). Never give a game module and a plugin module the same name — UBT compiles them into one rules assembly and requires the Build.cs class name to equal the module name. All game C++ goes in the **plugin** module. `Source/MassBattleGame/` stays empty. |
| 1.3 | **[machine]** Plugins enabled in `.uproject`: MassGameplay, MassAI, GameplayAbilities, MassBattle. **`MassEntity` and `FunctionalTesting` are NOT plugins in 5.8** (core engine modules) — never add them to `.uproject`/`.uplugin`; enabling by name fails UBT. |
| 1.4 | **[machine]** Module homes: `Engine/Source/Runtime/MassEntity` (MassEntity); `Engine/Source/Runtime/Mass/` contains MassCore, MassDeveloper, MassEngine, **MassSignals**; MassGameplay plugin modules include MassCommon, MassSpawner, MassSimulation, MassActors, MassLOD, MassMovement, MassRepresentation; MassAI plugin modules include MassNavigation, MassAIBehavior. Confirmed publicly: `UMassSignalSubsystem` header is `/Engine/Source/Runtime/Mass/MassSignals/Public/MassSignalSubsystem.h`; since 5.5 `FInstancedStruct` & co. live in CoreUObject (`#include "StructUtils/InstancedStruct.h"`, StructUtils plugin deprecated). |
| 1.5 | **[machine]** `UMassEntityConfigAsset` = `/Script/MassSpawner.MassEntityConfigAsset` (module MassSpawner — not MassEntity). Discovery pattern: `ObjectTools.search_subclasses(base_class={"refPath":"/Script/Engine.DataAsset"}, class_name="MassEntity")`. |
| 1.6 | **[machine]** Plugin `MassBattle.Build.cs` deps already correct for Phase 0: Core + CoreUObject, Engine, MassEntity, MassSpawner, MassSignals, MassCommon, GameplayAbilities. Add modules only when a task requires them (e.g. Gauntlet plugin module for the nightly controller — see Task 1). |
| 1.7 | **[machine]** Levels exist and load clean: `/Game/MassBattle/Demo/Maps/L_MassBattle_Demo01` and `/Game/MassBattle/Test/Maps/L_MassBattle_T0_Sandbox` (both: SkyLight/DirectionalLight/SkyAtmosphere/PlayerStart/Ground 256 m × 256 m). `MassDebugVisualizer_0` auto-respawns in Demo01 — harmless, ignore. |
| 1.8 | **[machine]** EDU fixture: `Data/EDU/golden_edu_v1.txt` — ASCII, LF-only, 2 unit types × 14 fields (corrected during the run; originally said 13 — reality wins, count the fixture: type, dictionary, category, class, soldier, attributes, formation, stat_health, stat_pri, stat_pri_armour, stat_mental, stat_charge_dist, stat_cost, ownership). **This file is the schema source of truth for T0.2** (do not invent field names; parse what the fixture contains). `.gitattributes` enforces `Data/**/*.txt text eol=lf`; data files stay ASCII (no em-dashes). |
| 1.9 | **[machine]** MCP server: `http://127.0.0.1:8000/mcp`, auto-starts with the editor (`bAutoStartServer=True`). No native MCP client in the harness — use the curl JSON-RPC helper pattern from `Docs/phases/pre-phase-0/report.md` §4.1. Keep calls **serial**. |
| 1.10 | **[machine]** Git: repo pre-exists on `main`, git-lfs 2.13.3 active for `*.uasset/*.umap/*.uproject`. Last commit: `prephase0: complete`. Commits this phase use prefix `phase0:`. |
| 1.11 | **[machine]** Reference machine (roadmap R10): i7-9700F 8C/8T, 16 GB RAM, GTX 1660 Ti, Win10 Home 19045 — recorded in `Docs/spec/reference-machine.md`; every perf baseline JSON must embed this spec (Task 1 defines the schema). |
| 1.12 | 5.8 is the **terminal UE5 release** (ADR-13). Public 5.8 facts used throughout §4: Mass Signals moved to engine core; sparse/virtual fragments & tags added (add/remove **without archetype change** — designed for rarely-present, frequently-toggled data); processor execution/dependency resolution overhauled with thread-safe observer notifications; off-game-thread entity creation now possible (game-thread-only remains a deliberate project choice per ADR-02/I1); Mass events visible in Unreal Insights; new optional simplified processor API `UE::Mass::FQueryExecutor`. |

## §2. MCP operating manual (do not rediscover)

Full transport recipe and tool-by-tool notes: `Docs/phases/pre-phase-0/report.md` §4. Quick-reference of the gotchas that matter every day:

| Tool | Recipe / gotcha |
|------|-----------------|
| Transport | POST `initialize` → capture `Mcp-Session-Id` response header → POST `notifications/initialized` → `tools/list` / `tools/call` with that header. Serial only. ~110 s per-call budget; test discovery is the slow call. |
| `LogsToolset.GetLogEntries` | Default `category` is `"LogsToolset"` — pass `"category": ""` to search the whole session log. This is your evidence channel for module loads and ExecBridge command output. |
| `SceneTools` | No `new_level`/`save_level`. Save via `AssetTools.save_assets(["/Game/..."])`; `load_level` refuses dirty packages — save before load; confirm success via `get_current_level`. |
| `AssetTools` | `duplicate` leaves the package dirty. Actor/class references are `{"refPath": "..."}` soft paths everywhere. |
| `DataAssetTools.create` | `{"refPath": "/Script/MassSpawner.MassEntityConfigAsset"}` creates a Mass config asset (pre-phase-0 pipeline proof). |
| `AutomationTestToolset` | `DiscoverTests` (once per session, slow) → `ListTests(nameFilter, tagFilter)` → `RunTests(testNames)` returns the completed JSON summary synchronously → `GetTestResults`. No editor restart needed. |
| `ProgrammaticToolset.execute_tool_script` | Sandbox: script defines `run()` returning a dict; imports limited to json/re/math/copy/datetime/time; **no `unreal` module**; calls tools via `execute_tool("<Qualified.Toolset.Tool>", json_string)`. |
| Console execution | Locate the console-command tool in your fresh `list_toolsets` output (Task 0) — this drives the ExecBridge commands of §4.11. If none exists, ExecBridge commands are invoked from inside automation tests via `World->Exec()` instead. |
| Restart batching | All `.uproject`/Build.cs/C++ edits → UBT build → **one** editor relaunch. Never restart for a single config tweak. After any restart: re-run `list_toolsets` and `DiscoverTests`. |

## §3. Ground rules (continuity with pre-phase-0)

1. **Progress log**: `Docs/phases/phase-0/progress.md`, one line per completed step: `[taskN.M] what was done → verification result`. Same format as `Docs/phases/pre-phase-0/progress.md`. Write it as you go, not at the end.
2. **Commits**: one commit per completed task, message prefix `phase0:`. Amend only pre-push local commits, as before.
3. **Evidence before claim**: every "done" carries tool evidence — a log line, a test JSON (`"state":"Success"`), `is_dirty=false`, `git log` hash. No exceptions.
4. **Reality wins** (§0): discrepancies are logged and the brief is corrected in place.
5. **Data files**: ASCII + LF only; em-dashes forbidden (use `--`).
6. **Test naming** (roadmap B.0): `Battle.Unit.<Area>.<Name>` (per-commit), `Battle.Func.S0_<Name>` (functional), `Battle.Data.*`, `Battle.Perf.*`, `Battle.Soak.*`. All test code behind `#if WITH_DEV_AUTOMATION_TESTS`. Test maps excluded from shipping cook.
7. **Pure-math seam** (roadmap B.0): every test-relevant algorithm is a free function or plain struct; processors are thin shims. Math inside a `ForEachEntityChunk` lambda is a review flag — at Phase 0 this applies to the EDU parser, handle math, state hash, and RNG registry.
8. **Never** add/remove tags or destroy entities inside `ForEachEntityChunk` — command buffer only (§4.5). Never cache `FMassActorFragment` in sim logic. No `FString`/`UObject*`/`TArray` in hot fragments (arch §3.1).

### §3.1 Owner-approved decisions (A1–A7, decided 2026-10-24) — pre-decided; do not reverse or re-litigate

| ID | Decision | Lands in |
|----|----------|----------|
| A1 | Phase 0 debug overlay ships **hooks only** (CVars, dump/counter endpoints, metrics format, MassInsights wiring); **all on-screen drawing moves to Phase 2** — the first drawable state (pairing, engagement records) only exists there. Approved deviation from roadmap B.2. | Task 12 |
| A2 | EDU **cook-time commandlet integration deferred to Phase 2**; Phase 0 ships the pure parser + golden tests and consumes rows via the editor-time import path. T0.2 unaffected. | Task 5 |
| A3 | **Sparse tag adopted for `FDeadTag`** (ADR-15, resolves roadmap Open Decision 7): killing an entity does **not** flip archetype — it is removed from live queries. Roadmap exit-criteria + T0.5 wording patched accordingly (with A7). | Tasks 9, 13 |
| A4 | **Jenkins LTS on the reference machine** is the R8 orchestration host; Windows Task Scheduler is only a documented stopgap if the Jenkins install is blocked; ADR-16 records the outcome either way. | Task 1 |
| A5 | T0.3 config assets are authored by the **ExecBridge command `Battle.BuildT0Configs`** (C++ builds the two `UMassEntityConfigAsset`s in-editor), not by hand via MCP property tools. PropertyTools authoring is the fallback if the command route is blocked. Rationale in Part II A5. | Task 6 |
| A6 | Risk watchlist adopted as a standing checklist for the run (§3.2). | §3.2 |
| A7 | Three spec patches to the in-repo roadmap/architecture docs, applied **during the run** (not up front): architecture §4 processor-signature note (after the Task 2 audit); roadmap Phase 0 exit-criteria + T0.5 wording (Task 9, with ADR-15); roadmap B.0 test-mode-contract reference (Task 11). | Tasks 2, 9, 11 |

### §3.2 Risk watchlist (approved — keep visible during the run)

1. `UMassSimulationSubsystem` may not tick in raw editor worlds — the §4.10 split (structure probes in editor world, tick-dependent asserts in PIE functional tests) is designed around this; if PIE-via-toolset proves flaky, the CI `func` tier headless run is the fallback.
2. Time-slicing cvar/settings names for T0.9 are the least-documented 5.7/5.8 area in public sources — Task 2 audit gets the exact names from source.
3. Gauntlet plugin's exact 5.8 location/state (Experimental?) — audit in Task 1; if `RunGauntlet` args changed, the doc page "Run Gauntlet Tests" is the reference.
4. Observer delegate signatures + `ObservedTypes` registration shape are new in 5.8 — expect one iteration in Task 9.
5. MCP console-command toolset existence is assumed but unverified — Task 0 checks; fallback (`World->Exec` from tests) is already wired into the plan.

## §4. Pre-researched UE 5.8.4 API digest (verify-then-trust)

> **Extracted 2026-10-25 (documentation reorg).** The digest now lives as **Part B** of `Docs/reference/engine-api-5.8.md`, alongside the Task 2 verification audit (Part A) and the research sources (Part C). Historical citations to "§4.x" or "BRIEF §4" resolve there. This stub keeps the brief's §-numbering intact for the progress log and report.

## §5. Phase 0 task plan

Scope = roadmap Phase 0 **in full** (owner decision: R8 CI included at full weight) **plus the owner-approved adjustments in §3.1 (A1–A5, A7)**. Ordering = CI first per the owner's priority directive; the roadmap's build order inside the data spine is otherwise preserved. One task = one commit (+ progress-log lines + gate evidence where applicable).

### Task 0 — Kickoff
1. Read order per §0; open `Docs/phases/pre-phase-0/report.md` §2/§4 beside you.
2. Refresh MCP: `list_toolsets` → update `Docs/reference/mcp-playbook.md` if the toolset list differs (note console-command tool location for §4.11).
3. `git status` clean check on `main`; create branch `phase0` (or continue on `main` if the pre-phase-0 protocol did — follow repo convention).
4. Create `Docs/phases/phase-0/progress.md` with the session header.

**Verify**: MCP handshake OK; editor responsive; clean tree. **Commit**: `phase0: kickoff`.

### Task 1 — CI foundation (R8) — **highest priority**
1. **Headless gate script** `Tools/CI/run_tests.bat` (tiers as arg: `unit` = Battle.Unit+Battle.Data+Battle.Smoke via UnrealEditor-Cmd per §4.12; `func` = Battle.Func). Log-parse pass/fail; writes `Tools/CI/last_run.json` (tier, date, counts, machine tag).
2. **Baseline storage**: `Baselines/perf/README.md` + schema doc; `Tools/CI/compare_baseline.py` (Python 3 stdlib only) implementing `{machine, engineCL, date, metrics}` compare with tolerance arg.
3. **Orchestration host (decided — §3.1 A4)**: install/configure **Jenkins LTS** controller+agent on the reference machine (document the install); add `Jenkinsfile` implementing the per-commit pipeline (build → unit/data gate → artifact the logs). Only if the Jenkins install is blocked by environment constraints: implement the Task-Scheduler stopgap **and record ADR-16 with the blocker** — do not silently downgrade R8.
4. **Nightly job**: scheduled task/Jenkins cron → `Tools/CI/nightly.bat`: build editor + package Win64 (`RunUAT BuildCookRun -cook -stage -pak` on the game target) → run the Gauntlet smoke controller (next step) on the staged build → append baseline JSON → `git commit` baselines.
5. **Gauntlet stub**: enable the Gauntlet plugin in `.uproject` (audit location per §4.12), add `UBattleSmokeController : UGauntletTestController` (wait/assert-no-errors/EndTest) in the plugin module behind `#if WITH_DEV_AUTOMATION_TESTS`-equivalent gating appropriate to the plugin's module type; register via `-gauntlet=`.
6. Add `[Zen.AutoLaunch] LimitProcessLifetime=false` to `DefaultEngine.ini` (§4.12).
7. First manual run of the nightly pipeline end-to-end; commit the first two baseline JSONs (spawn-time metric lands in Task 6 — record placeholder metric names now).

**Gates hit**: none directly, but T0.9's settings harness and all later evidence flows through this.
**Verify**: `run_tests.bat unit` green locally against the existing `Battle.Smoke.PluginLoaded`; nightly job produces staged build + Gauntlet log + baseline JSON; `git log` shows the baseline commit.
**Commit**: `phase0: CI foundation (R8) — gate script, Jenkinsfile, nightly, gauntlet stub, baselines`.

### Task 2 — Pinned-API audit (roadmap I6)
Audit every ⚠️ entry in §4 against `E:\Unreal\UE_5.8\Engine\Source` (+ engine plugin sources). Produce `Docs/reference/engine-api-5.8.md` as a table: symbol → status (✅/⚠️/❌) → actual 5.8.4 signature → source file path → notes. This file is cited by every later task; it is also the reusable template for future engine upgrades (ADR-13's scripted-diff goal — a text checklist is sufficient this phase; the scripted diff can graduate later).

Then apply the first approved spec patch (§3.1 A7): the architecture doc's §4 processor table gets a note recording the 5.8 signatures — `ConfigureQueries(const TSharedRef<FMassEntityManager>&)`, `Execute(FMassEntityManager&, FMassExecutionContext&)`, ctor-registered queries — citing this audit.

**Verify**: audit file complete, every ⚠️/❌ in §4 resolved; discrepancies logged and this brief's §4 lines corrected in place; architecture §4 patch applied.
**Commit**: `phase0: pinned API audit (I6)`.

### Task 3 — Data spine headers + ADR-14 probe
1. Implement in `Plugins/MassBattle/Source/MassBattle/`:
   - `Public/BattleTypes.h`: `FUnitHandle{uint32 Index, Serial}` (+ `IsValid()`, `GetTypeHash`), `FUnitRuntimeState` plain struct (roster `TArray<FMassEntityHandle>`, cached attribute snapshot, generation counter, engagement-record stub = empty for Phase 0), `FUnitSpawnSpec` (EDU row + count + formation stub).
   - `Public/BattleFragments.h`: Phase-0 fragment set per arch §3.1 — `FOwnerUnitFragment`, `FLoadoutFragment` (EDU row index + mesh/weapon IDs), `FBaseStatsFragment` (immutable, EDU-mapped), `FHealthFragment`, `FUnitStatsSharedFragment` (§4.2, ADR-14 anchor first); tags `FInfantryTag`, `FCavalryTag`, `FMeleeTag`, `FBowTag`, `FOfficerTag`, `FDeadTag` (declared like any tag; it is *added/removed* via the 5.8 sparse-tag path per ADR-15 — Task 9).
   - `Public/BattleTags.h` if you prefer tags separate; `LogMassBattle` category declaration.
2. **ADR-14 probe test** `Battle.Unit.Mass.SharedFragUniqueness` (world test per §4.10): create 8 shared fragments via `GetOrCreateSharedFragmentByHash` with identical payloads *except* the handle seed → assert 8 distinct instances; mutate one → others unchanged. (This is the API-level probe; the full T0.8 spawn-level assertion lands in Task 13.)
3. UBT build gate; smoke test re-run.

**Gates**: contributes to T0.8 (probe half). **Verify**: build exit 0; probe test Success. **Commit**: `phase0: data spine fragments + ADR-14 probe`.

### Task 4 — Registry subsystem + T0.1
1. `UUnitRegistrySubsystem` per §4.7 + arch §2.3: handle alloc/free with serial counters, free-list reuse, roster bookkeeping, dirty-flag API (`MarkStatsDirty`), `RegisterUnit/UnregisterUnit`, EXEC commands `Battle.DumpRegistry`, `Battle.StateHash` (hash skeleton lives in Task 10 — emit a stable placeholder now).
2. Unit tests `Battle.Unit.Registry.*`: stale-handle rejection (generation counter), free-list reuse safety, 10k spawn/destroy round-trip leak-free, dirty-flag idempotence (T0.1).

**Gates**: **T0.1** ✅. **Verify**: test JSON `Success`; `EXEC_RESULT DumpRegistry` sane after manual probe. **Commit**: `phase0: registry subsystem + T0.1`.

### Task 5 — EDU parser + T0.2
1. `Public/BattleEduParser.h/.cpp` — **pure functions** (§3 rule 7): parse fixture schema (13 fields, exact set read from `Data/EDU/golden_edu_v1.txt`), strict tokenizer, unknown-field rejection, canonical serializer. Canonicalization rules (write them in the parser header): fixed field order, single-space normalization, lowercase fixed tokens, integer/float normalization (no leading zeros; floats ≤2 decimals, trailing zeros trimmed), comment lines preserved only if the fixture format requires them for round-trip — otherwise dropped per roadmap R11 ("canonical form, byte-identical is over-strict").
2. `Battle.Data.EduRoundTrip`: fixture → tables → canonical text → compare against canonicalized fixture; second synthetic golden with edge cases (extra whitespace, unknown field → expect failure, numeric normalization). Unknown-field test asserts a **loud failure** (roadmap: "rejected loudly").

**Scope note (§3.1 A2, approved)**: the cook-time commandlet integration of the parser is **deferred to Phase 2** — do not add the `UCommandlet` or cook wiring in this phase; Phase 0 consumes parsed rows via the editor-time import path.

**Gates**: **T0.2** ✅. **Verify**: test JSON Success ×2. **Commit**: `phase0: EDU parser + T0.2`.

### Task 6 — Spawn path + entity configs + T0.3
1. `UBattleSpawnLibrary` (static functions, plugin module): given `FUnitSpawnSpec` + parsed EDU row → Route A spawn (archetype-first per §4.8) with per-role archetypes + ADR-14 shared-fragment creation at fixup + `FOwnerUnitFragment`/`FBaseStatsFragment`/`FHealthFragment` fixup; roster registered in registry; spawn timing recorded (µs) and logged as `EXEC_RESULT SpawnPerf` (baseline metric for Task 1's storage).
2. Authoring (decided — §3.1 A5): the `Battle.BuildT0Configs` ExecBridge command creates the two `UMassEntityConfigAsset` assets (Peasant Levy, Sergeant Spearmen) with minimal trait/fragment sets matching the C++ archetypes **[implementation detail still per Task 2 audit: trait/`BuildTemplate` route vs direct template construction — record which you used in ADR notes]**, then save via `AssetTools.save_assets` (§2); store under `/Game/MassBattle/Data/Units/`. Pure-MCP PropertyTools authoring is the fallback only if the command route is blocked — log it if used.
3. T0.3 test `Battle.Data.EntityConfigIntegrity`: both assets resolve fragments/tags, archetype buildable, roster specs instantiate from the parsed table.

**Gates**: **T0.3** ✅; T0.8 full assertion precondition ready. **Verify**: EXEC_RESULT lines for two rosters (2 units × 40 entities); T0.3 Success; spawn-perf baseline JSON committed. **Commit**: `phase0: spawn path + configs + T0.3`.

### Task 7 — Unit actor + GAS attribute stubs
1. `AUnitActor` (plugin module): `UAbilitySystemComponent` + `UUnitAttributeSet` (closed set §4.9); `PostInitializeComponents`: `InitAbilityActorInfo`, register all attribute-change delegates → `MarkStatsDirty(Handle)`; owns `FUnitSpawnSpec` and performs the Task 6 spawn on demand (game-thread, outside processing — §4.5 guard).
2. Unit tests `Battle.Unit.AttributeSet.*`: closed set completeness (all §10.1 attributes exist), delegate fires on `SetNumericAttributeBase`, dirty flag set exactly once per change burst.

**Verify**: tests Success. **Commit**: `phase0: unit actor + GAS attribute stubs`.

### Task 8 — UnitSync processor + T0.4
1. `UUnitSyncProcessor` per §4.3 (game thread, PrePhysics, `Battle` group, first in pipeline — `ExecutionOrder.ExecuteAfter` audited empties or explicit ordering vs engine defaults; record the final ordering in the audit file).
2. `Battle.SetAttr` ExecBridge command → attribute change; functional test `Battle.Func.S0_SyncLatency` (map `L_MassBattle_T0_Functional`, `AFunctionalTest` subclass): set attribute on unit A → assert `FUnitStatsSharedFragment` reflects the new value within 1 tick (assert on tick counter, not wall time — roadmap T0.4); also asserts unit B's fragment unaffected (spawn-level T0.8 second half).

**Gates**: **T0.4** ✅ (+ T0.8 spawn-level ✅). **Verify**: functional test Success via `AutomationTestToolset` (or CI `func` tier). **Commit**: `phase0: UnitSync + T0.4/T0.8`.

### Task 9 — Death path + ADR-15 + T0.5
1. **ADR-15 — DECIDED (§3.1 A3, owner-approved): adopt the 5.8 sparse tag for `FDeadTag`** (resolves roadmap Open Decision 7). Task 2's audit supplies the API; the sparse probe in this task confirms it on-machine. Rationale: official guidance matches exactly ("rarely present, frequently toggled"), and it eliminates the hottest archetype churn (mass-death events) ahead of Phase 2. Record ADR-15 in the architecture doc's decision log + rationale + the audit citation; keep the fallback (regular tag + command buffer) behind a compile-time switch in case the on-machine probe fails. In the same commit, apply the approved roadmap wording patch (§3.1 A7): wherever the roadmap says "killing an entity **flips archetype**" (Phase 0 exit criteria, T0.5 expectation text), replace with "killing an entity removes it from live queries (sparse tag or archetype flip per ADR-15), fires the death signal, and updates the roster".
2. `UBattleDeathObserver` (5.8 observer API, `ObservedTypes` incl. `FDeadTag`) → `SignalEntityDeferred("Battle.UnitDeath", Entity)`; `UBattleDeathSignalReceiver : UMassSignalProcessorBase` binds the signal → reads `FOwnerUnitFragment` → registry roster update → `SendGameplayEventToActor(UnitActor, "Battle.Unit.Died", ...)`. Deferred `DestroyEntity` queued after the signal (corpse policy is Phase 5).
3. Functional test `Battle.Func.S0_DeathPath`: kill one entity via `Battle.KillEntity` → assert: entity absent from live queries (query with `FDeadTag` presence `None` counts 0) at the **audited flush point** (buffer-state assert post-processor or end-of-phase — E5 contract, record which in a code comment); signal received; roster updated; GAS event observed; queued destroy completes.

**Gates**: **T0.5** ✅; ADR-15 recorded. **Verify**: functional test Success; ADR-15 committed. **Commit**: `phase0: death path + ADR-15 + T0.5`.

### Task 10 — State hash + RNG registry + T0.6/T0.7
1. `FBattleStateHash` (pure): fold registry rosters/serials + per-entity quantized state (transforms 1 cm, HP 1e-2, enum states) + archetype count + shared-fragment values into a CityHash64 (UE `CityHash.h`). Exclude wall-clock, frame counters, and any non-sim state. `Battle.StateHash` ExecBridge emits it.
2. `FBattleRngRegistry` (pure struct + thin subsystem): named `FRandomStream` streams, per-battle root seed → `HashCombine(root, GetTypeHash(name))`; `Battle.RngSelfTest` command.
3. Tests: `Battle.Unit.StateHash.Stability` (identical state → identical hash; any seeded mutation changes it — T0.6); `Battle.Unit.Rng.Discipline` (suite green under 5 different root seeds; two registries with equal roots produce identical 10k-draw sequences — T0.7).

**Gates**: **T0.6**, **T0.7** ✅. **Commit**: `phase0: state hash + RNG registry + T0.6/T0.7`.

### Task 11 — Test-mode contract + T0.9
1. CVar group `Battle.Test.*`: `Battle.Test.FixedStep` (drives `FApp::SetUseFixedTimeStep` + value), `Battle.Test.DisableTimeSlicing` (sets the audited 5.7/5.8 time-slicing settings), `Battle.Test.ForceFullDetail` (SimLOD/variable-tick off — audit the relevant settings).
2. `UBattleTestSettings::AssertTestMode()` helper: reads **active** engine settings (fixed-step flag, time-slicing state, LOD scheduling state) and `AddError`s on violation — invoked at the top of every functional test (roadmap: assert on active settings, not on CVars being set).

Then apply the approved roadmap B.0 patch (§3.1 A7): the roadmap's "test-mode contract" section now references the `Battle.Test.*` cvar group and the `UBattleTestSettings::AssertTestMode()` helper created by this task.

**Gates**: **T0.9** ✅ (helper + one test proving it fails when a setting is deliberately violated). **Commit**: `phase0: test-mode contract + T0.9`.

### Task 12 — Debug overlay skeleton (B.2, slimmed per §3.1 A1)
1. `UBattleDebugSubsystem` + `Battle.Debug.*` CVars: on-screen toggle hook point (draw implementation arrives with Phase 2 — nothing visual exists yet), log-dump endpoints (registry, shared fragments, per-unit aggregates), counters stub (per-frame processor ms, archetype count — recorded into the B.4 metrics format now, drawn later).
2. Wire MassInsights/Mass Debugger cross-check note (roadmap I2): confirm in the audit that 5.8 Insights shows Mass events; record the verification path in `Docs/reference/engine-api-5.8.md`. Do **not** hand-roll metrics the debugger already exposes.

**Scope note (§3.1 A1, approved)**: hooks only — CVars, dump/counter endpoints, metrics format, MassInsights wiring. **No HUD/canvas drawing code in this phase**; all on-screen visualization (pairing lines, coverage heat, unstuck colors) moves to Phase 2, where the first drawable state exists.

**Verify**: CVars toggle without errors; dumps produce `EXEC_RESULT` evidence; archetype-count metric visible. **Commit**: `phase0: debug overlay skeleton`.

### Task 13 — Exit-criteria scenario
End-to-end on `L_MassBattle_T0_Functional` (or Demo01 if world type demands): spawn **2 units × 40 entities from the parsed EDU table** → apply a GAS attribute change on unit A → assert shared-fragment visibility ≤ 1 frame (T0.4 re-assert) and unit-B isolation (T0.8 re-assert) → kill one entity → assert removal from live queries (sparse tag per ADR-15 — no archetype flip expected), signal, roster update (T0.5 re-assert) → emit `Battle.StateHash` before/after → record spawn-perf baseline (Task 1 storage) → run the **full** per-commit tier headless via `Tools/CI/run_tests.bat` as the final green run.

### Task 14 — Handoff report
Write `Docs/phases/phase-0/report.md` in the prephase0-report format: paths, facts-digest corrections, deviations, tool quirks, verbatim evidence, DoD checklist (§7), "where Phase 1 should start". Final commit `phase0: complete`. **STOP.**

## §6. T0 gate → implementation map

| Gate | Test / artifact | Route | Task |
|------|-----------------|-------|------|
| T0.1 | `Battle.Unit.Registry.*` (stale handles, reuse, 10k round-trip) | AutomationTestToolset, per-commit tier | 4 |
| T0.2 | `Battle.Data.EduRoundTrip` (+ unknown-field loud failure) | per-commit tier | 5 |
| T0.3 | `Battle.Data.EntityConfigIntegrity` | per-commit tier | 6 |
| T0.4 | `Battle.Func.S0_SyncLatency` (tick-count assertion) | functional map + toolset | 8 |
| T0.5 | `Battle.Func.S0_DeathPath` (audited flush-point assert + signal + roster) | functional map + toolset | 9 |
| T0.6 | `Battle.Unit.StateHash.Stability` | per-commit tier | 10 |
| T0.7 | `Battle.Unit.Rng.Discipline` (5 seeds × suites) | per-commit tier | 10 |
| T0.8 | API probe (Task 3) + spawn-level assertions in `S0_SyncLatency` | both | 3, 8 |
| T0.9 | `UBattleTestSettings::AssertTestMode()` + violation-detection test | per-commit tier | 11 |

## §7. Definition of done (checklist for the handoff report)

- [ ] All T0.1–T0.9 green, evidence JSON captured in the report (state `Success`, 0 failed)
- [ ] Exit criteria (roadmap Phase 0, wording per §3.1 A3/A7): 2×40 entities spawned from table data; attribute change visible in `FUnitStatsSharedFragment` ≤ 1 frame; death → removed from live queries (sparse tag, ADR-15) + signal + roster update
- [ ] R8 CI: gate script + Jenkins LTS (§3.1 A4; ADR-16 records the decision, or the enforced Task-Scheduler fallback), nightly packaged-build job incl. Gauntlet stub controller, baseline JSON storage with ≥ 2 real entries, compare script, Zen config
- [ ] ADR-15 recorded (decided: adopt sparse tag — §3.1 A3); ADR-16 (CI host) recorded
- [ ] `Docs/reference/engine-api-5.8.md` complete (I6) — every §4 ⚠️ resolved
- [ ] Approved spec patches applied (§3.1 A7): architecture §4 signature note (Task 2), roadmap exit-criteria + T0.5 wording (Task 9), roadmap B.0 test-mode reference (Task 11)
- [ ] `Docs/phases/phase-0/progress.md` complete; `phase0: complete` commit exists
- [ ] B.8 DoD per capability: unit tests over math, scenario test, debug overlay hook, metric, regression-checklist entry, `Battle.*` cvar flag (registry/EDU/spawn/sync/death paths each checked)
- [ ] Editor left clean (no dirty maps), MCP responsive, all evidence reproducible

## §8. Agent system prompt (paste-able)

```text
You are the MassBattle Phase 0 implementation agent. You execute
Docs/BRIEF_phase0.md autonomously, exactly like the pre-phase-0 run.

NON-NEGOTIABLES
1. Facts: Docs/phases/pre-phase-0/report.md §2 and BRIEF §1 are authoritative. If reality
   contradicts them, reality wins: correct the doc in place, log the
   discrepancy, keep moving.
2. Token discipline: never re-research anything BRIEF §4 states — cite it.
   Never read beyond the §0 read-order until a task points you there.
3. MCP: serial JSON-RPC via the documented curl helper; restart batching
   (batch all file-side work → UBT → one relaunch); re-inventory after every
   restart. Editor restarts are the scarcest resource.
4. Evidence before claim: every "done" has tool evidence (log line, test JSON,
   is_dirty, git hash). No claim without it.
5. One commit per task, prefix "phase0:". Progress log line per step in
   Docs/phases/phase-0/progress.md, format [taskN.M] what → verification.
6. Scope lock: build exactly Task 1–14. Do not pull Phase 1+ work forward
   (no spatial hash, no slots, no servo). Roadmap-faithful; deviations only
   where reality forces them, and each deviation is logged with reason.
   Exception: the decisions in BRIEF §3.1 (A1–A5, A7) are owner-approved —
   implement them exactly as written; do not reverse or re-litigate them.
7. Data files ASCII + LF. Test names per B.0. All game C++ in
   Plugins/MassBattle/Source/MassBattle/ (game module stays empty).
8. When blocked: try the BRIEF's documented fallback; if still blocked after
   two distinct approaches, stop that task, write the blocker to the progress
   log with full evidence, continue with any independent task, and surface
   the blocker in the final report. Never fabricate success.
9. Priority directive: Task 1 (CI foundation) is the owner's #1 priority —
   land it completely before starting the data-spine tasks.

STOP CONDITION
All of §7 DoD checked → write Docs/phases/phase-0/report.md (prephase0-report
format) → final commit "phase0: complete" → stop.
```

---

# PART II — DECISION LOG (owner verdicts 2026-10-24)

**Verdicts: A1–A4, A6, A7 approved; A5 delegated to preparer judgment (adopted — ExecBridge route); A8 not requested.** Everything approved is already merged into Part I (§3.1 table + the task sections). The notes below are kept for rationale; the executing agent does not need them.

### A1. Debug overlay: skeleton now, drawing later — APPROVED → Task 12
Roadmap B.2 says "build it now, not later". Proposal: Phase 0 ships the **hooks** (§5 Task 12: cvar group, dump/counter endpoints, metrics format, MassInsights wiring) and defers all **drawing** (pairing lines, coverage heat, unstuck colors) to Phase 2, where the first drawable state (pairing, engagement records) actually exists. Nothing in Phase 0–1 produces data those overlays would show; the roadmap's own MassInsights adoption (I2) already covers structural visualization. Cost saved: days of HUD work. Risk: none identified — B.8's "a debug overlay" per capability is satisfied by the hook points until Phase 2.

### A2. EDU cook-time integration deferred — APPROVED → Task 5
Roadmap Phase 0 says "EDU-format entity table parsed at cook". Proposal: Task 5 ships the pure parser + golden tests; the cook-time commandlet hook (one `UCommandlet` + postload wiring) moves to Phase 2 when the content pipeline hardens. Phase 0 consumes parsed rows via the editor-time import path; T0.2 is unaffected. Rationale: removes editor/cook integration risk from the critical path with zero gate impact.

### A3. Sparse-tag adoption + roadmap wording patches — APPROVED → Tasks 9/13 + §7
Recommend adopting 5.8 sparse tags for `FDeadTag` (ADR-15, already forced by roadmap Open Decision 7). Consequence: the roadmap exit criterion "killing an entity **flips archetype**" becomes wrong under sparse tags (that's the point — no archetype move). Proposed wording: "killing an entity removes it from live queries (sparse tag or archetype flip per ADR-15), fires the death signal, and updates the roster". Same patch in T0.5's expectation text. Also pre-flag: §4 of the architecture doc and §B.0 of the roadmap reference pre-5.6 processor idioms (`RegisterWithProcessor`, manager-less `ForEachEntityChunk`) — the I6 audit will surface them; the doc patches in A7 keep the specs honest.

### A4. CI host recommendation — APPROVED → Task 1
Within R8's "Horde / Jenkins / hosted CI" menu: **Jenkins LTS on the reference machine** is the recommendation baked into Task 1 (single-machine fit, JUnit-ish report parsing, artifact retention, trivial cron). Horde assumes farm infrastructure far beyond one box; hosted CI adds remote-runner friction with local LFS + licensing. The Task-Scheduler stopgap is documented in case Jenkins install is blocked, with ADR-16 recording the decision either way.

### A5. Config-asset authoring route — ADOPTED (owner delegated the judgment call) → Task 6
T0.3 needs `UMassEntityConfigAsset` assets to exist and resolve. Authoring them by hand through MCP property tools is brittle (traits are instanced UObjects). Task 6 uses the ExecBridge `Battle.BuildT0Configs` command instead (C++ builds the assets in-editor). Technique note, not a scope change — but if you'd rather stay pure-MCP for asset authoring, say so and Task 6 will do PropertyTools-based authoring with a fallback.

**Judgment (owner delegated this call): adopted the ExecBridge route.** Why: (1) traits are instanced UObjects, so authoring them through generic MCP property tools means brittle nested-property writes with no atomicity and easy silent breakage; (2) the C++ command is deterministic and repeatable — both configs can be regenerated with one command after any refactor; (3) it emits `EXEC_RESULT` evidence lines, matching the project's evidence protocol (§3 rule 3); (4) `Battle.BuildT0Configs` is already in the Phase 0 command set (§4.11), so the marginal cost is near zero. The PropertyTools fallback stays wired into Task 6.

### A6. Risk watchlist (for the run) — APPROVED → §3.2
1. `UMassSimulationSubsystem` may not tick in raw editor worlds — the §4.10 split (structure probes in editor world, tick-dependent asserts in PIE functional tests) is designed around this; if PIE-via-toolset proves flaky, the CI `func` tier headless run is the fallback.
2. Time-slicing cvar/settings names for T0.9 are the least-documented 5.7/5.8 area in public sources — Task 2 audit gets the exact names from source.
3. Gauntlet plugin's exact 5.8 location/state (Experimental?) — audit in Task 1; if `RunGauntlet` args changed, the doc page "Run Gauntlet Tests" is the reference.
4. Observer delegate signatures + `ObservedTypes` registration shape are new in 5.8 — expect one iteration in Task 9.
5. MCP console-command toolset existence is assumed but unverified — Task 0 checks; fallback (`World->Exec` from tests) is already wired into the plan.

### A7. Suggested spec patches — APPROVED → Tasks 2/9/11
- Roadmap Phase 0 exit criteria + T0.5 wording (archetype flip → query removal, per ADR-15).
- Roadmap §B.0 "test-mode contract": reference `Battle.Test.*` cvar group + `AssertTestMode()` helper once they exist (Task 11).
- Architecture §4 processor table: note 5.8 signatures (`ConfigureQueries(const TSharedRef<FMassEntityManager>&)`, `Execute(FMassEntityManager&, FMassExecutionContext&)`, ctor-registered queries) in the next revision, citing the I6 audit.

### A8. Optional accelerators — NOT REQUESTED (owner listed A1–A7 only; still available on demand)
- I can draft the full text of `Docs/reference/engine-api-5.8.md` as a pre-filled checklist (symbol → expected signature → blank verdict column) so Task 2 is pure confirmation.
- I can pre-write `Tools/CI/run_tests.bat`, `compare_baseline.py`, and the `Jenkinsfile` skeletons now so Task 1 becomes copy-in + verify.
- I can draft ADR-15/ADR-16 texts in advance for the agent to ratify.

---

# PART III — Research sources (verified 2026-10-24)

> **Extracted 2026-10-25 (documentation reorg).** Now **Part C** of `Docs/reference/engine-api-5.8.md`.

| Source | Used for |
|---|---|
| MassSample README — github.com/Megafunk/MassSample (5.8-updated Jun 2026) §4.2.1, §4.4, §4.6–4.7 | shared-fragment hash semantics (E1), subsystem MT traits, processor/query tables, accessor table |
| vrealmatic.com/unreal-engine/mass (explicitly "reflects Unreal Engine 5.8") | 5.1→5.8 change table, 5.8 processor skeleton, ExecutionFlags incl. EditorWorld, sparse fragments, observer API change, traits/BuildTemplate |
| UE 5.8 release notes — dev.epicgames.com | Mass overhaul (signals→core, sparse, off-GT creation, Insights), Horde status, Zenserver `LimitProcessLifetime=false` |
| Epic API docs — UMassSignalSubsystem (Runtime/MassSignals) | exact signal signatures + header path |
| Epic "Simplified Mass Processor/Query API (Query Executor)" page + nukethebees.com quickstart | FQueryExecutor option, `AutoExecuteQuery`, `AllWorldModes`, archetype-first creation calls |
| Gauntlet doc pages (dev.epicgames.com, 5.8): Framework / Overview / Run Gauntlet Tests / Gauntlet Controller | nightly orchestration design, `UGauntletTestController`, RunUAT invocation |
| Epic forums: "Correct way to create mass entities" (Jan 2026), "Mass Signal vs. Observer processor" (Jan 2026), automation command-line threads; StackOverflow automation answer | spawner usage discipline (IsProcessing/Defer), two SpawnEntities overloads, `-ExecCmds`/`-TestExit` headless pattern |
| Epic "Write C++ Tests in Unreal Engine"; unreal-garden testing tutorial | `IMPLEMENT_SIMPLE_AUTOMATION_TEST`, flags, `FAutomationEditorCommonUtils` |
| `Docs/phases/pre-phase-0/report.md` §2/§4 (on-machine) | all [machine] facts in §1/§2 |
