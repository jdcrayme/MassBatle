# Engine API Reference — UE 5.8.4 (pinned)

| | |
|---|---|
| **Status** | Living reference — canonical home for every pinned-engine API fact (**one fact, one home**, `Docs/AGENTS.md` §7). |
| **Engine pin** | UE 5.8.4 (CL 58902052, `++UE5+Release-5.8`) at `E:\Unreal\UE_5.8` (ADR-13) |
| **Provenance** | Part A: pinned-API audit from Phase 0 Task 2 (2026-10-24, formerly `Docs/agent/api-audit-5.8.md`). Part B: §4 of the Phase 0 brief, corrections applied in place. Part C: research sources. Consolidated 2026-10-25. |
| **Correction rule** | When reality contradicts an entry: correct it **here** and log the change in the current phase's progress log. Do not re-open engine source to re-derive a fact already recorded here — cite the section. |

> Historical note: earlier documents refer to this content as "BRIEF §4", "§4.x", "the audit", or "`Docs/reference/engine-api-5.8.md`" — all of those citations resolve to this file now.

# Part A — Pinned-API audit (verification record)

**Engine pin:** UE 5.8.4, CL 58902052, `++UE5+Release-5.8`, `E:\Unreal\UE_5.8` (ADR-13).
**Method:** every ⚠️ entry of BRIEF §4 checked against the pinned engine source on this machine
(2026-10-24, Phase 0 run). Each entry: verdict → actual signature → source path → notes.
**Legend:** ✅ confirmed as brief stated · ⚠️ signature/shape differs (actual written) · ❌ absent (replacement noted).
**This file is cited by every later task.** After Task 2, later tasks cite entries here instead of re-opening engine source.
It doubles as the reusable upgrade checklist for ADR-13's scripted-diff goal.

---

## 0. Corrections summary (reality-wins; BRIEF §4 lines corrected in place)

| # | BRIEF §4 said | 5.8.4 reality | Brief line fixed |
|---|---------------|---------------|------------------|
| C1 | §4.1: "`RegisterWithProcessor()` no longer exists in 5.8" | **Exists**: `FMassEntityQuery::RegisterWithProcessor(UMassProcessor& Owner)` (MassEntityQuery.h:86) and ctor `FMassEntityQuery(UMassProcessor& Owner)` (:81). Ctor-registration is the pattern; the method still exists for late registration. | ✅ edited |
| C2 | §4.6: receiver "binds `SignalReceiveDelegate` in `ConfigureSignals()`" | **No such members in 5.8.** Actual: override `SignalEntities(FMassEntityManager&, FMassExecutionContext&, FMassSignalNameLookup&)` (PURE_VIRTUAL) + `SubscribeToSignal(UMassSignalSubsystem&, const FName)`; optional `OnSignalReceived(FName, TConstArrayView<FMassEntityHandle>)`. | ✅ edited |
| C3 | §4.7: traits `enum { ThreadSafeRead = true, ThreadSafeWrite = false }` | Actual members: `enum { GameThreadOnly = <bool>, ThreadSafeWrite = <bool> }` (defaults: GameThreadOnly=true, ThreadSafeWrite=!GameThreadOnly). Header moved to MassCore: `#include "Mass/ExternalSubsystemTraits.h"` (old `MassExternalSubsystemTraits.h` UE_DEPRECATED_HEADER(5.8)). | ✅ edited |
| C4 | §4.10: `FApp::SetFixedTimeStepValue(...)` / `FApp::IsUseFixedTimeStep()` | **Absent.** Actual: `FApp::SetUseFixedTimeStep(bool)` + `FApp::SetFixedDeltaTime(double)` / `FApp::GetFixedDeltaTime()` / `FApp::UseFixedTimeStep()` (all gated `WITH_FIXED_TIME_STEP_SUPPORT`). Consumed by `UEngine::TickWorldTiming` (UnrealEngine.cpp:3005). | ✅ edited |
| C5 | §4.12: "audit whether 5.8 supports `-ReportOutputPath`" | Answer: renamed. `-ReportExportPath=<dir>` writes `<dir>/index.json` + `index.html` (UTF-8 **BOM** — parse with utf-8-sig). Old name logs a deprecation warning but still parses. | ✅ edited |
| C6 | §4.2: `BuildContext.RequireFragment<T>()` | Not found in 5.8 `FMassEntityTemplateBuildContext`. Actual surface: `AddFragment()`, `AddFragment_GetRef<T>()`, `AddTag(...)`, `AddChunkFragment()`, `AddSharedFragment(FSharedStruct)`; trait-side extra requirements via `UMassEntityTraitBase::FAdditionalTraitRequirements`. | ✅ edited |
| C8 | §4.2 ADR-14 creation code: `EntityManager.GetOrCreateSharedFragmentByHash<FUnitStatsSharedFragment>(Seed, Payload)` as a public call | **Private** in 5.8 (see §1 row); Task 3 probe + Task 6 spawn use the public routes (content-CRC create / archetype-value attach) with the ADR-14 anchor carried in the payload | ✅ edited |
| C10 | §4.6 (ADR-15 plan): observe sparse elements via UMassObserverProcessor | **Unsupported in 5.8.4**: `UMassObserverRegistry::RegisterObserver` ensures `!IsSparse(ObservedType)` -- "observing sparse elements is not supported at the moment" (MassObserverRegistry.cpp:25; on-machine probe crashed boot) | ✅ edited |
| C11 | §4.2 (ADR-15 plan): sparse tag add is per-entity without archetype change | **Chunk-granular in 5.8.4**: sparse TAG storage is a per-chunk bitset ("Sparse tags live there since they don't contain any data per instance", MassSparseElementsStorage.h) -- AddTag on one entity marks its whole chunk. Per-entity death marking requires a regular tag | ✅ edited |
| C12 | §4.7 audit row: "MassCore is a MassEntity dependency -- no extra Build.cs entry needed" | The base-struct UClass constructors need **MassCore linked explicitly** (LNK2019 without it); added to Build.cs private deps at Task 3 | ✅ edited |
| C7 | §4.2: fragments/tags derive `FMassFragment`/`FMassTag` "engine auto-registers" — module not stated | Base structs live in **MassCore**: `#include "Mass/EntityElementTypes.h"` (`FMassFragment`, `FMassSparseFragment`, `FMassTag`, `FMassSparseTag`, `FMassChunkFragment`, `FMassSharedFragment`, `FMassConstSharedFragment`). MassCore is a MassEntity dependency — no extra Build.cs entry needed. | ✅ edited |

## 1. Fragments, tags, shared fragments (§4.2)

| Symbol | Verdict | Actual 5.8.4 | Source | Notes |
|---|---|---|---|---|
| `GetOrCreateSharedFragmentByHash` | ⚠️ | **PRIVATE** in 5.8 (all overloads, MassEntityManager.h:932-957). Public routes: (a) `GetOrCreateSharedFragment(const T&)` (:1073) / `GetOrCreateSharedFragment<T>(args...)` (:1136) -- hash = `UE::StructUtils::GetStructInstanceCrc32(payload)` (:1064), i.e. content CRC; (b) archetype attach: `FMassArchetypeSharedFragmentValues::AddSharedFragment(FSharedStruct)` + `CreateArchetype`/`BatchCreateEntities` -- the per-unit route Phase 0 uses | `MassEntity/Public/MassEntityManager.h` | ADR-14 adapts: the FUnitHandle anchor makes payloads value-distinct, so the content-CRC route and the archetype-value route both yield per-unit instances; a compile-time switch `MASSBATTLE_ADR14_EXPLICIT_HASH` would need a friend declaration and is not warranted |
| `GetOrCreateSharedFragment<T>(...)` | ✅ | forwards to ByHash (hash from payload) | MassEntityManager.h:1084,1136 | Do NOT use for per-unit fragments (E1) |
| `FMassArchetypeCompositionDescriptor` | ✅ | struct at MassEntityTypes.h:76 (Fragments/Tags/SharedFragments bitsets) | `MassEntity/Public/MassEntityTypes.h:76` | |
| `FMassArchetypeSharedFragmentValues` | ✅ | `FSharedStruct AddSharedFragment(const FSharedStruct&)` (:485) | MassEntityTypes.h:339-485 | |
| `CreateArchetype` | ✅ | `FMassArchetypeHandle CreateArchetype(const FMassArchetypeCompositionDescriptor&, const FMassArchetypeCreationParams& = {})` (:193); + list-based overloads (:159,:172,:183) | MassEntityManager.h:159-193 | |
| `BatchCreateEntities` | ✅ | `TSharedRef<FEntityCreationContext> BatchCreateEntities(const FMassArchetypeHandle&, const FMassArchetypeSharedFragmentValues&, const int32 Count, TArray<FMassEntityHandle>&)` (:345; no-shared overload :346) | MassEntityManager.h:345-348 | Returns creation context |
| `BatchReserveEntities` | ✅ | `TConstArrayView<FMassEntityHandle> BatchReserveEntities(const int32 Count, TArray<FMassEntityHandle>&)` | MassEntityManager.h:586 | |
| Route B `UMassSpawnerSubsystem::SpawnEntities` | ✅ | exactly 2 overloads: `(const FMassEntityTemplate&, const uint32 NumberToSpawn, TArray<FMassEntityHandle>&)` and `(FMassEntityTemplateID, const uint32, FConstStructView SpawnData, TSubclassOf<UMassProcessor> InitializerClass, TArray<FMassEntityHandle>&)` | `Engine/Plugins/Runtime/MassGameplay/Source/MassSpawner/Public/MassSpawnerSubsystem.h:33,35` | Template-based (E4 confirmed); both return `TSharedPtr<FMassEntityManager::FEntityCreationContext>` |
| `UMassEntityTraitBase::BuildTemplate` | ✅ | `virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const` (PURE_VIRTUAL) | MassSpawner/Public/MassEntityTraitBase.h:51 | |
| BuildContext methods | ⚠️ (C6) | `AddFragment()` / `AddFragment(TNotNull<const UScriptStruct*>)` / `AddFragment(FConstStructView)` / `T& AddFragment_GetRef<T>()` / `AddTag()` (+ typed) / `AddChunkFragment()` / `AddSharedFragment(const FSharedStruct&)` | MassSpawner/Public/MassEntityTemplate.h:130-260 | `RequireFragment<T>` absent |
| Sparse types | ✅ | `FMassSparseFragment : FMassFragment` (:30), `FMassSparseTag : FMassTag` (:50) | `Engine/Source/Runtime/Mass/MassCore/Public/Mass/EntityElementTypes.h` | ADR-15 route: `struct FDeadTag : FMassSparseTag` |
| Sparse concept | ✅ | `UE::Mass::CSparse<T>` (TIsDerivedFrom FMassSparseFragment/FMassSparseTag); `UE::Mass::IsSparse(...)` runtime check | MassEntity/Public/MassEntityConcepts.h:44,64 | |
| Sparse commands | ✅ | command buffer AddTag/RemoveTag "Supported types: FMassFragment, FMassTag, FMassSparseFragment, FMassSparseTag" | MassEntity/Public/MassCommands.h:372,658,765 | Same deferred API as regular tags; no archetype change |
| Sparse requirements | ✅ | `AddTagRequirement<T>(Presence)` auto-routes sparse (`if constexpr (CSparse<T>)`); explicit `AddSparseRequirement(ElementType, Presence)` incl. `None` | MassEntity/Public/MassRequirements.h:260-401 | T0.5 "absent from live queries" = query with `AddTagRequirement<FDeadTag>(EMassFragmentPresence::None)` |
| Chunk shared accessor | ✅ | `T& GetMutableSharedFragment<T>()` (:613), `const T& GetConstSharedFragment<T>()` (:586) | MassEntity/Public/MassExecutionContext.h | |
| Out-of-query shared mutation | ✅ | `FSharedStruct::GetMutableMemory()` exists (SharedStruct.h:92) | `CoreUObject/Public/StructUtils/SharedStruct.h` | Query route preferred (arch §5.1); direct mutation legal game-thread-only |

## 2. Processors & queries (§4.3, §4.4)

| Symbol | Verdict | Actual 5.8.4 | Source | Notes |
|---|---|---|---|---|
| `ConfigureQueries` | ✅ | `UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)` — pure virtual on UMassProcessor | `MassEntity/Public/MassProcessor.h:225` | |
| `Execute` | ✅ | `UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)` | MassProcessor.h:233 | |
| Query registration | ✅ (C1) | `FMassEntityQuery(UMassProcessor& Owner)` ctor + `RegisterWithProcessor(UMassProcessor&)` | MassEntity/Public/MassEntityQuery.h:81,86 | `FMassEntityQuery : FMassFragmentRequirements, FMassSubsystemRequirements` (:50) — all requirement methods live on the bases |
| `bAutoRegisterWithProcessingPhases` | ✅ | `uint8 : 1 = true` (MassProcessor.h:252) | MassProcessor.h | |
| `SetProcessingPhase` | ✅ | `UE_API virtual void SetProcessingPhase(EMassProcessingPhase)`; property default `EMassProcessingPhase::PrePhysics` | MassProcessor.h:125,244 | |
| `ExecutionOrder` | ✅ | `FMassProcessorExecutionOrder ExecutionOrder` (with ExecuteInGroup/ExecuteAfter/ExecuteBefore FNames) | MassProcessor.h:238 | Built-in groups: `UE::Mass::ProcessorGroupNames` (`MassCommon/Public/MassCommonTypes.h:18` — UpdateWorldFromMass, SyncWorldToMass, Behavior, Tasks, Avoidance, ApplyForces, Movement, ...) |
| `ExecutionFlags` | ⚠️ | raw `uint8` bitfield, **no inline default**; CDO ctor sets `Server | Standalone` (MassProcessor.cpp:138). Values: None/Standalone/Server/Client/Editor/EditorWorld + AllNetModes/AllWorldModes/All (MassProcessingTypes.h:23-35) | MassProcessor.h:248; MassProcessor.cpp:138 | Brief's claim confirmed: must set `EProcessorExecutionFlags::AllWorldModes` explicitly for editor-world execution |
| `bRequiresGameThreadExecution` | ✅ | `uint8 : 1 = false` | MassProcessor.h:259 | |
| `UE::Mass::FQueryExecutor` | ✅ (present) | alternative simplified API exists (MassQueryExecutor.h — `AddSharedRequirement<TFragment>` usage at :459) | `MassEntity/Public/MassQueryExecutor.h` | Not used in Phase 0 (classic style per brief) |
| `AddRequirement<T>(Access, Presence)` | ✅ | templated on FMassFragmentRequirements; duplicate-requirement checkf | MassEntity/Public/MassRequirements.h:238 | |
| `AddTagRequirement<T>(Presence)` | ✅ | auto sparse routing (see §1) | MassRequirements.h:~322 | |
| `AddSharedRequirement<T>(Access, Presence)` | ✅ | templated + by-struct; `Any` presence invalid (checkf) | MassRequirements.h:534,568 | |
| `AddConstSharedRequirement<T>(Presence)` | ✅ | `Any` invalid | MassRequirements.h:476 | |
| `AddSubsystemRequirement` | ✅ | query-level `AddSubsystemRequirement(const TSubclassOf<USubsystem>, EMassFragmentAccess)` (MassEntityQuery.h:144; template re-exported via `using` at :142); processor-level `FMassSubsystemRequirements ProcessorRequirements` member (MassProcessor.h:329, getter :134) | | |
| Chunk accessors | ✅ | `GetFragmentView<T>()` (:640), `GetMutableFragmentView<T>()` (:630), `GetNumEntities()` (:447), `Defer()` (:437), `GetChunkFragment<T>()` (:562) | MassEntity/Public/MassExecutionContext.h | |
| Subsystem access in chunk | ✅ | `GetSubsystemChecked<T>(World)` / `GetMutableSubsystemChecked<T>(World)` | MassExecutionContext.h | Standard usage unchanged |

## 3. Command buffers & flush contract (§4.5, review E5)

| Item | Verdict | 5.8.4 reality | Source |
|---|---|---|---|
| `Defer()` | ✅ | `FMassCommandBuffer& FMassExecutionContext::Defer()` (:437); `FMassEntityManager::Defer()` const (:910) | MassExecutionContext.h / MassEntityManager.h |
| Add/RemoveFragment, Add/RemoveTag, DestroyEntity | ✅ | `AddFragment(Entity)` (:183), `RemoveFragment` (:198), `AddTag` (:214, +array :221), `RemoveTag` (:237, +array :244), `DestroyEntity` (:344); `*_RuntimeCheck` variants deprecated 5.8 | MassEntity/Public/MassCommandBuffer.h |
| `PushCommand<T>` | ✅ | templated (:105-163) | MassCommandBuffer.h |
| `FMassDeferredCreateCommand` | ✅ | listed in the command table ("Arbitrary logic at flush") | MassCommands.h:89 |
| `IsProcessing()` | ✅ | `bool FMassEntityManager::IsProcessing() const` | MassEntityManager.h:905 |
| **Flush contract (E5)** | ✅ resolved | (a) **Per-processor scope**: after a processor's tasks complete, its processing scope is destroyed and `ExecutionContext.SetFlushDeferredCommands(true); ExecutionContext.FlushDeferred()` runs — observers are unlocked *before* the flush (`OnPreCommandFlushNotification`) and execute during it (MassExecutor.cpp:147-154; MassProcessingQueue.cpp:308-318). (b) **Phase end**: `FMassProcessingPhaseManager` flushes the entity-manager buffer if pending (MassProcessingPhaseManager.cpp:664-666). (c) Query contexts flush deferred commands on pop (MassEntityQuery.cpp:76). | MassEntity/Private/* |
| Batch element API rename | ✅ | `BatchAddElementToEntities` family exists (MassCommands.h, MassEntityManager.h, MassSparseElementsStorage.h); not used by Phase 0 (deferred commands only) | grep |

**T0.5 consequence:** "directly post-Death-processor" is valid — the Death processor's deferred commands are applied when its scope ends, i.e. before the *next processor in the same phase* observes state. T0.5 asserts with a follow-up query after the Death processor completes within the same phase; the test code records this contract (also noted in architecture §4 note, below).

## 4. Observers & Signals (§4.6)

| Symbol | Verdict | Actual 5.8.4 | Source | Notes |
|---|---|---|---|---|
| `UMassSignalSubsystem` API | ✅ | all as brief stated: `SignalEntity(FName, const FMassEntityHandle)` (:45), `SignalEntities(FName, TConstArrayView<FMassEntityHandle>)` (:52), `DelaySignalEntity` (+Entities) (:60,68), `SignalEntityDeferred(FMassExecutionContext&, FName, const FMassEntityHandle)` (:76), `SignalEntitiesDeferred` (:84), `DelaySignalEntityDeferred` (+Entities) (:93,102), `UE::MassSignal::FSignalDelegate& GetSignalDelegateByName(FName)` (:35) | `Engine/Source/Runtime/Mass/MassSignals/Public/MassSignalSubsystem.h` | Module MassSignals (core, `Runtime/Mass/MassSignals`) |
| Receiver processor | ⚠️ (C2) | `UMassSignalProcessorBase`: override `virtual void SignalEntities(FMassEntityManager&, FMassExecutionContext&, FMassSignalNameLookup& EntitySignals)` (PURE_VIRTUAL); subscribe with `SubscribeToSignal(UMassSignalSubsystem&, const FName SignalName)`; optional `OnSignalReceived(FName, TConstArrayView<FMassEntityHandle>)`; base overrides `Execute`; `ShouldAllowQueryBasedPruning=false`; `ConfigureQueries(const TSharedRef<FMassEntityManager>&)` overridable; member `FMassEntityQuery EntityQuery` | `MassSignals/Public/MassSignalProcessorBase.h` | No `ConfigureSignals`/`SignalReceiveDelegate` — the 5.8 shape |
| Observer registration | ✅ | ctor: `ObservedTypes = { FType::StaticStruct(), ... }; ObservedOperations = EMassObservedOperationFlags::Add;` (`bAutoRegisterWithObserverRegistry=true` default; virtual `Register()` from CDO PostInitProperties) | `MassEntity/Public/MassObserverProcessor.h:60-133` | |
| Deprecated observer fields | ✅ | single `ObservedType` UE_DEPRECATED(5.8), `Operation` UE_DEPRECATED(5.7); `GetObservedTypeChecked` deprecated → `GetSingleObservedTypeChecked` | MassObserverProcessor.h | |
| Operation flags | ✅ | `EMassObservedOperation`: AddElement, RemoveElement, DestroyEntity, CreateEntity (+deprecated Add/Remove aliases). Flags: None/AddElement/RemoveElement/CreateEntity/DestroyEntity + combos Add=AddElement|CreateEntity, Remove=RemoveElement|DestroyEntity, All | `MassEntity/Public/MassEntityTypes.h:687-717` | |
| Observer execution context | ✅ | `FMassObserverExecutionContext` via `FMassExecutionContext::GetAuxData()` — `GetOperationType()`, `GetTypesInOperation()`, `GetCurrentType()`; multi-type notifications dedup per entity batch | MassObserverProcessor.h:23-56 | Death observer reads aux data to branch on operation |

## 5. World subsystems / registry pattern (§4.7)

| Symbol | Verdict | Actual 5.8.4 | Source | Notes |
|---|---|---|---|---|
| `TMassExternalSubsystemTraits<T>` | ⚠️ (C3) | `template <typename T> struct TMassExternalSubsystemTraits final { enum { GameThreadOnly = true, ThreadSafeWrite = !GameThreadOnly }; }` — customize `GameThreadOnly=false` (worker-readable) + `ThreadSafeWrite=false` for the registry | `Engine/Source/Runtime/Mass/MassCore/Public/Mass/ExternalSubsystemTraits.h:26` | Include from **MassCore**; `MassExternalSubsystemTraits.h` deprecated shim remains |
| `UE_MT_DECLARE_RW_ACCESS_DETECTOR` | ✅ | `#define UE_MT_DECLARE_RW_ACCESS_DETECTOR(AccessDetector) FRWAccessDetector AccessDetector` | `Core/Public/Misc/MTAccessDetector.h:730` | |
| `MassGameplayExternalTraits.h` pattern | ✅ | file exists under MassGameplay (separate-header trait specializations) | `MassGameplay/Source/MassGameplayExternalTraits` | Use if traits can't live in the registry header |

## 6. Spawning & entity creation (§4.8)

| Symbol | Verdict | Actual 5.8.4 | Source |
|---|---|---|---|
| `UMassEntitySubsystem::GetMutableEntityManager()` | ✅ | `FMassEntityManager& GetMutableEntityManager()` (:40); `const FMassEntityManager& GetEntityManager() const` (:35) | `MassEntity/Public/MassEntitySubsystem.h` |
| Route A calls | ✅ | see §1 rows (CreateArchetype / SharedFragmentValues / BatchReserve / BatchCreate) | MassEntityManager.h |
| Route B | ✅ | SpawnEntities ×2 + traits (see §1) | MassSpawnerSubsystem.h / MassEntityTraitBase.h |
| `UMassEntityConfigAsset` | ✅ (pre-phase-0) | `/Script/MassSpawner.MassEntityConfigAsset` | MassSpawner module |

## 7. GAS (§4.9)

| Symbol | Verdict | Actual 5.8.4 | Source |
|---|---|---|---|
| `SetNumericAttributeBase` | ✅ | `UE_API void SetNumericAttributeBase(const FGameplayAttribute& Attribute, float NewBaseValue)` | `GameplayAbilities/Public/AbilitySystemComponent.h:233` |
| `GetGameplayAttributeValueChangeDelegate` | ✅ | `UE_API FOnGameplayAttributeValueChange& GetGameplayAttributeValueChangeDelegate(FGameplayAttribute Attribute)` | AbilitySystemComponent.h:539 |
| `SendGameplayEventToActor` | ✅ | `static UE_API void SendGameplayEventToActor(AActor* Actor, FGameplayTag EventTag, FGameplayEventData Payload)` | `GameplayAbilities/Public/AbilitySystemBlueprintLibrary.h:83` |
| `InitAbilityActorInfo` | ✅ | stable API (used engine-wide) | AbilitySystemComponent.h |

## 8. Automation & test harness (§4.10)

| Item | Verdict | 5.8.4 reality | Source |
|---|---|---|---|
| Fixed time step | ⚠️ (C4) | `FApp::SetUseFixedTimeStep(bool)`; `FApp::SetFixedDeltaTime(double)` / `GetFixedDeltaTime()`; `FApp::UseFixedTimeStep()`; all `WITH_FIXED_TIME_STEP_SUPPORT`-gated | `Core/Public/Misc/App.h:557,673`; `Core/Public/Misc/AppTime.h:68-212` |
| Engine consumption | ✅ | `UEngine::TickWorldTiming`: `bUseFixedTimeStep = IsBenchmarking() || UseFixedTimeStep()` → per-frame `SetDeltaTime(GetFixedDeltaTime())` | `Engine/Private/UnrealEngine.cpp:3005-3020` |
| `UWorld::CreateWorld` | ✅ | `UWorld* CreateWorld(EWorldType::Type, bool bInformEngineOfWorld, FName WorldName = NAME_None, UPackage* = nullptr, bool bAddToRoot = true, ERHIFeatureLevel::Type = ..., const InitializationValues* = nullptr, bool bSkipInitWorld = false)` | `Engine/Private/World.cpp:2817` |
| Headless gate run | ✅ machine-verified | `-ExecCmds="Automation RunTests <filter>; Quit" -TestExit="Automation Test Queue Empty"` + `-nullrhi` runs the full tier and exits (used by `Tools/CI/run_tests.bat`) | this session, Task 1 |
| Summary lines | ✅ | per-test `LogAutomationController: Test Completed. Result={<state>} Name={<name>} Path={<path>}`; end `**** TEST COMPLETE. EXIT CODE: <n> ****`; exit code reflects failures | `Developer/AutomationController/Private/AutomationControllerManager.cpp:43`; `AutomationCommandline.cpp:503` |
| `-ReportExportPath` | ⚠️ (C5) | writes `index.json` (+html) with UTF-8 BOM; JSON has top-level `succeeded/succeededWithWarnings/failed/notRun/totalDuration` (no per-test array in index) | `AutomationControllerManager.cpp:213-237,367` |

## 9. ExecBridge (§4.11)

| Symbol | Verdict | Actual 5.8.4 | Source |
|---|---|---|---|
| `FAutoConsoleCommandWithWorldAndArgs` | ✅ | ctor `(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldAndArgsDelegate& Command, uint32 Flags = ECVF_Default)` (+Flags-first overload) | `Core/Public/HAL/IConsoleManager.h:2411` |

## 10. CI stack (§4.12) — machine-verified this run

| Item | Verdict | 5.8.4 reality |
|---|---|---|
| Gauntlet plugin | ⚠️ | `Engine/Plugins/Experimental/Gauntlet` (Experimental dir; uplugin `IsBetaVersion: true`); Runtime module, PreDefault loading; deps Core/CoreUObject/Engine (+UnrealEd when editor). Controllers: `UGauntletTestController` with `OnInit/OnTick/...`, created for `-gauntlet=<Name>` when a UClass equals `<Name>` or ends with `<Name>Controller` (GauntletModule.cpp:196-240); `EndTest(code)` → `RequestExitWithStatus` (standalone exit works — verified on-machine: staged build exited after `TEST COMPLETE. EXIT CODE: 0`/`-1`). Note: on Windows graceful-quit can still yield process exit 0 even for `EndTest(-1)` — log line is authoritative |
| RunUAT verb | ⚠️ | `RunUnreal` (not `RunGauntlet`), requires Gauntlet test-node assemblies in namespaces `Gauntlet.UnrealTest,UnrealGame,UnrealEditor` — Phase 0 nightly drives the native game-side framework directly instead (ADR-16) |
| Zen | ✅ applied | `[Zen.AutoLaunch] LimitProcessLifetime=false` in project DefaultEngine.ini |

## 11. T0.9 settings audit (risk §3.2.2 — time-slicing names from source)

| Mechanism | 5.8.4 reality | Source | T0.9 assertion |
|---|---|---|---|
| Processor time-slicing | **No global cvar/setting.** `UE::Mass::FExecutionLimiter` is opt-in per call (`ForEachEntityChunk(Context, Limiter, Fn)`, explicit `EntityLimit`). If a processor never constructs a limiter, nothing is sliced. | `MassEntity/Public/MassEntityTypes.h:43`; `MassEntityQuery.h` overload | Compile-time: Battle processors never construct `FExecutionLimiter` (asserted by code review + a unit test greping... no — a static_assert-free convention + documentation). Runtime helper asserts `FApp::UseFixedTimeStep()` etc. (below). |
| Query parallelism switch | `mass.AllowQueryParallelFor` (default true, ECVF_Cheat) | `MassEntity/Private/MassEntityQuery.cpp:30` | T0.9 helper reads it and asserts the *determinism-relevant* state (we keep it true — chunk execution stays deterministic for pure reads; documented, not disabled) |
| Variable tick | Per-entity opt-in: `UMassLODTrait::bEnableVariableTicking = false` default + `FMassVariableTickChunkFragment` only added by that trait | `MassLOD/Public/MassLODTrait.h:52`; `MassLODFragments.h:51` | Runtime: assert Phase-0 archetypes carry no variable-tick chunk fragment (query probe) |
| Simulation switches | `mass.SimulationTickingEnabled`, `mass.EntityCompaction` (cvars); `UMassSimulationSettings` (config=Mass): Desired*TimeSlicePerTick budgets (spawning/compaction, not processor slicing) | `MassSimulation/Private/MassSimulationSubsystem.cpp:25-31`; `MassSimulation/Public/MassSimulationSettings.h` | Asserted readable in helper; compaction left on (does not affect determinism of fragments) |
| Fixed step | see §8 | | `FApp::UseFixedTimeStep()` true + `FApp::GetFixedDeltaTime()` == expected |

**T0.9 implementation note (Task 11):** there is no global time-slicing cvar to "disable" -- the contract cvar `Battle.Test.DisableTimeSlicing` records the contract and the structural guarantee is compile-time (Battle processors never construct `FExecutionLimiter`; kept true by review per B.0). `UBattleTestSettings::ApplyTestMode()` seeds the -game process; `AssertTestMode()` reads the ACTIVE state (FApp fixed step + delta, contract cvars).

**T0.9 conclusion:** the roadmap's "time-slicing disabled" contract is satisfied *structurally* in 5.8 (opt-in API we never use) and asserted *actively* for what is readable: fixed-step flag + delta, simulation ticking enabled, no variable-tick fragment, no MassLOD trait on test archetypes. `UBattleTestSettings::AssertTestMode()` (Task 11) implements exactly these reads.

## 12. MassInsights / Mass Debugger (roadmap I2, BRIEF Task 12)

**Task 12 verification (complete).** Trace/insights surface present in the pinned source: `MassEntity/Public/MassEntityTrace.h`, `MassEntity/Public/MassDebugger.h`, `MassEntity/Public/MassDebuggerBreakpoints.h` (archetype-move events, sparse-element visibility, batched entity-move traces per §4.1). Verification path for future runs: launch with `-trace=` and open the trace in Unreal Insights -- the Mass channel appears when the MassEntity module loads (no project wiring required); the Mass Debugger window lists archetypes/entities live in editor sessions. Phase 0 hand-rolled metrics (Battle.DumpMetrics: archetype count, per-frame UnitSync ms, unit count, bookkeeping hash) stay limited to what the debugger does not already expose (B.2/I2 rule). No engine work needed -- the wiring is engine-side; recorded here as the I2 check.

## 13. Processor ordering decision (BRIEF Task 8 requirement — filled at Task 8)

UnitSync runs `PrePhysics`, group `Battle` (custom), `ExecutionFlags = AllWorldModes`, `bRequiresGameThreadExecution = true`. Final ordering vs engine defaults recorded here in Task 8. **(pending Task 8)**

## 14. Movement-stack audit (Phase 1 Task 1, 2026-10-24)

Scope: the Phase 1 brief §4 digest verified row-by-row against pinned source. Module homes verified: MassMovement = `Engine/Plugins/Runtime/MassGameplay/Source/MassMovement/` (24 files); MassNavigation = `Engine/Plugins/AI/MassAI/Source/MassNavigation/` (see C13). Under decision A1 the engine movement/avoidance stack is **reference-only**; MassMovement is linked (for `FMassVelocityFragment`), MassNavigation is not linked.

### Corrections C13–C20 (brief §4/§1 lines corrected in place per §4 flow)

| # | Brief said | 5.8.4 reality | Brief line fixed |
|---|------------|---------------|------------------|
| C13 | §1.8/§4.1: MassNavigation at `/Engine/Plugins/Runtime/MassAI/Source/MassNavigation/` | **`Engine/Plugins/AI/MassAI/Source/MassNavigation/`** — the MassAI plugin lives under `Plugins/AI/`, not `Plugins/Runtime/` (MassGameplay is under `Runtime/`). | ✅ edited |
| C14 | §4.2: goal force `Force.Value = SteerK * (Steering.DesiredVelocity - Velocity.Value)` | Reference velocity is the **previous desired velocity**, not actual: `Force.Value = SteerK * (Steering.DesiredVelocity - DesiredMovement.DesiredVelocity)` (MassSteeringProcessors.cpp:237); engine comment: "stable against noisy velocity ... dont want to read the actual agent velocity directly". `SteerK = 1/MovingSteeringParams.ReactionTime` confirmed (:110). | ✅ edited |
| C15 | digest omission | `UMassApplyMovementProcessor` **hard-requires `FMassCodeDrivenMovementTag`** (All) and excludes `FSpringMovementSettings` + `FMassCustomMovementTag` (MassMovementProcessors.cpp:85-88): the engine's default mover only moves code-driven-tagged entities. | ✅ edited |
| C16 | §4.2: "clamps by `FMassMovementParameters` (MaxSpeed / MaxAcceleration) apply in this area" | ApplyForce clamps desired velocity to **MaxSpeed only** (`GetClampedToMaxSize(MovementParams.MaxSpeed)`, MassMovementProcessors.cpp:57). MaxAcceleration is used only in the standing-steering force clamp (MassSteeringProcessors.cpp:344), never by ApplyForce. | ✅ edited |
| C17 | §4.3 `FMassMoveTargetFragment` "[verify] hard or optional" | **Hard requirement** of both `UMassMovingAvoidanceProcessor` (MassAvoidanceProcessors.cpp:296) and `UMassStandingAvoidanceProcessor` (:1121). Additionally both **hard-require `FMassNavigationEdgesFragment`** (:295, :1120) — engine avoidance presumes the nav-edges pipeline. Strengthens A1 (adoption would drag in move targets + nav edges). | ✅ edited |
| C18 | §4.4 item 4: "Is grid iteration order deterministic?" | Yes, given deterministic insertion: `FindCloseObstacles` sorts candidate cells by squared distance before iteration (MassAvoidanceProcessors.cpp:53-101) and per-agent obstacle lists are distance-sorted (:815-822, :1277). Caveat: standing steering uses `FMath::RandRange` (MassSteeringProcessors.cpp:272,304 — nondeterministic); `UE::RandomSequence::FRand(Entity.Index)` at :259 is deterministic. Option B contingency stays viable but carries C17's fragment prerequisites. | ✅ edited |
| C19 | deferred item: "`MassEntityHandle.h` include → `Mass/EntityHandle.h` before next engine release" | Confirmed: `MassEntity/Public/MassEntityHandle.h` is a `UE_DEPRECATED_HEADER(5.8)` shim forwarding to `Mass/EntityHandle.h` (MassCore; already linked). Both project includes migrated at Task 1 (BattleTypes.h, BattleFuncTestActor.h). | ✅ done |
| C20 | digest omissions (new facts) | (a) The movement input fragment is **`FMassDesiredMovementFragment`** `{DesiredVelocity, DesiredFacing, DesiredMaxSpeedOverride}` (MassMovementFragments.h:35) — the digest's "DesiredVelocity" is its field. (b) ApplyForce/ApplyMovement clamp delta time `FMath::Min(0.1f, DT)` (MassMovementProcessors.cpp:41,100). (c) ApplyForce **zeroes `Force.Value`** after integration (:60) — the accumulator is per-frame. | ✅ edited |

### Verified rows (movement-stack symbol table)

| Symbol | Verdict | Actual 5.8.4 | Source |
|---|---|---|---|
| `FMassVelocityFragment` | ✅ | `FVector Value` ("actual physical velocity of the mass entity") | `MassMovement/Public/MassMovementFragments.h:19` |
| `FMassDesiredMovementFragment` | ✅ (C20a) | `DesiredVelocity` + `DesiredFacing` + `DesiredMaxSpeedOverride=FLT_MAX` | MassMovementFragments.h:35 |
| `FMassForceFragment` | ✅ | `FVector Value`, per-frame accumulator, zeroed by ApplyForce | MassMovementFragments.h:47; MassMovementProcessors.cpp:60 |
| `FMassMovementParameters` | ✅ | `MaxSpeed=200` (cm/s) · `MaxAcceleration=250` (cm/s²) · `DefaultDesiredSpeed=140` · `DefaultDesiredSpeedVariance=0.1` · `HeightSmoothingTime=0.2s` · `MovementStyles[]` · `bIsCodeDrivenMovement=true`; all units cm-explicit via `ForceUnits` metadata — **no cm-vs-m mixing** | MassMovementFragments.h:82-151 |
| `FMassCodeDrivenMovementTag` / `FMassCustomMovementTag` | ✅ (C15) | tags gating ApplyMovement / opting out of it | MassMovementFragments.h:64,75 |
| `FAgentRadiusFragment` | ✅ | `float Radius = 40.f` — centimeters | `MassCommon/Public/MassCommonFragments.h:11-16` |
| `UMassApplyForceProcessor` | ✅ | `DesiredVelocity = (Old + Force*Min(0.1,DT)).GetClampedToMaxSize(MaxSpeed)`; group ApplyForces, ExecuteAfter Avoidance; query: DesiredMovement RW + Force RW + OffLOD None + MovementParameters const-shared All | `MassMovement/Public/Movement/MassMovementProcessors.h`; body `Private/MassMovementProcessors.cpp:38-63` |
| `UMassApplyMovementProcessor` | ✅ | `Velocity = DesiredVelocity` (code-driven: direct assign, no smoothing); `Translation += Velocity*Min(0.1,DT)`; **no ground snap, no Z handling** (height smoothing is `UMassNavigationSmoothHeightProcessor`, MassNavigation) | MassMovementProcessors.cpp:96-146 |
| `UMassSimpleMovementProcessor` / `UMassSimpleMovementTrait` | ✅ | minimal reference: `Transform += Velocity*DT`, honors variable-tick fragment when present; runs in group **Avoidance** (not Movement); Example-folder code | `MassMovement/Public/Example/MassSimpleMovementTrait.h:22,32`; body `Private/MassSimpleMovementTrait.cpp` |
| Spring movement family | ✅ (note-only) | `MassSpringMovement*.h` + own group name `UE::Mass::ProcessorGroupNames::SpringMovement` (MassMovementTypes.h:11) | `MassMovement/Public/MassSpringMovement*.h` |
| `UMassSteerToMoveTargetProcessor` | ✅ | order confirmed: ExecuteAfter Tasks, ExecuteBefore Avoidance (cpp:62-63); goal-force formula per C14; Move/Stand/Animate action branches; hard-requires MoveTarget + Steering + StandingSteering + Ghost + Force + DesiredMovement fragments | `MassNavigation/Public/Steering/MassSteeringProcessors.h`; body `Private/MassSteeringProcessors.cpp` |
| `FMassMoveTargetFragment` | ✅ (C17) | engine move target (Center/Forward/DesiredSpeed/action state) | `MassNavigation/Public/MassNavigationFragments.h:18` |
| `FMassSteeringFragment` | ✅ | `DesiredVelocity` cache — "Note: not used for moving the entity" | `MassNavigation/Public/Steering/MassSteeringFragments.h:16-25` |
| `UMassMovingAvoidanceProcessor` / `UMassStandingAvoidanceProcessor` | ✅ | Experimental (header comment "Experimental: move using cumulative forces"); requirements per C17/C18 | `MassNavigation/Public/Avoidance/MassAvoidanceProcessors.h:20,37`; body `Private/MassAvoidanceProcessors.cpp` |
| `FMassMovingAvoidanceParameters` / `FMassStandingAvoidanceParameters` | ✅ | const shared fragments (tunable docs in header) | `MassNavigation/Public/Avoidance/MassAvoidanceFragments.h:14,118` |
| `UMassNavigationObstacleGridProcessor` | ✅ | exists as named (feeds obstacle grid; paired `UMassNavigationObstacleRemoverProcessor`) | `MassNavigation/Public/MassNavigationProcessors.h:72` |
| `UE::Mass::ProcessorGroupNames::Avoidance / ApplyForces / Movement` | ✅ | used verbatim as ctor group assignments in the audited processors | MassMovementProcessors.cpp:25-26,76-77; MassSteeringProcessors.cpp:62-63 |
| Debug surface | ✅ | `LogAvoidance`, `LogAvoidanceVelocities`, `LogAvoidanceAgents`, `LogAvoidanceObstacles` categories declared (MassAvoidanceProcessors.h:11-14); UE_VLOG segments throughout steering/avoidance | MassAvoidanceProcessors.h |

**§4.4 item 6 — velocity-fragment adoption decision:** the project **adopts engine `FMassVelocityFragment`** for per-entity velocity (recorded as an arch §3.1 note, A7 pattern): it is the exact concept the arch row names ("position, velocity, servo output"), it is a trivial blittable struct with no behavioral coupling, and MassMovement is linked anyway — a duplicate project `FVelocityFragment` would be a second name for the same bytes and a future query/processor mismatch hazard. Servo writes it; Integrate consumes it; no engine processor touches battle entities (A1).

### Discrepancies vs Phase 1 brief §4 — all resolved

C13–C20 above. None change the A1 decision: the engine pipeline remains reference-only; C17 (nav-edges hard requirement) and C18 (sorted, mostly-deterministic grid iteration) are recorded for the Option B contingency.

---

### Discrepancies vs BRIEF §4 — all resolved

C1–C7 (§0 above). None of them change the Phase 0 architecture; C2 (signal receiver shape) and C3 (traits members) change Task 4/9 code shape exactly as recorded here. The brief's §4 lines were corrected in place at Task 2 time; no other ⚠️ remains unresolved.

---

# Part B — Operational API digest (from Phase 0 brief §4, corrections applied in place)

> **Citation compatibility:** headings below keep the Phase 0 brief's original §4.0–§4.13 numbering, so historical instructions like "cite the digest entry (§4.3)" or "BRIEF §4" resolve here. Where Part A verdicts and this digest ever appear to disagree, **Part A wins** (it was verified line-by-line against the pinned engine source).

## §4. Pre-researched UE 5.8.4 API digest (verify-then-trust)

### §4.0 Protocol

Every entry below carries a **verification status**. In Task 2 you audit each entry against the pinned 5.8.4 source (`E:\Unreal\UE_5.8\Engine\Source\...`) and record one of: ✅ confirmed · ⚠️ signature differs (write the actual) · ❌ absent (find the replacement, note the search path). Result goes to `Docs/reference/engine-api-5.8.md`. After Task 2, later tasks **cite** the audit instead of re-opening engine source. Entries marked ⚠️*pre-verified* below were already confirmed by the public 5.8 sources in Part III during this brief's research pass; audit is still mandatory but should be fast.

### §4.1 Version-history cheat table (why old tutorials are wrong)

| Version | Change that affects this project |
|---------|----------------------------------|
| 5.5 | MassEntity became an engine runtime module; StructUtils deprecated (FInstancedStruct → CoreUObject). |
| 5.6+ | `ConfigureQueries(const TSharedRef<FMassEntityManager>&)` (parameterless = legacy); queries register by construction — `FMassEntityQuery Query(*this)` — `RegisterWithProcessor(UMassProcessor&)` still exists (audit C1: MassEntityQuery.h:86) but ctor registration is the pattern; `ForEachEntityChunk(Context, ...)` no longer takes the entity manager; optional new `UE::Mass::FQueryExecutor` API. |
| 5.7 | Processor time-slicing (`UE::Mass::FExecutionLimiter`) — T0.9 must verify it is off under test; observer calls are batched while command buffers flush (relevant to T0.5/E5). |
| 5.8 | Mass Signals in engine core (`Runtime/Mass/MassSignals`); **sparse/virtual fragments & tags** (add/remove without archetype change — designed for rarely-present, frequently-toggled data; `FDeadTag` decision, roadmap Open Decision 7); reworked processor scheduling + thread-safe observers; off-game-thread entity creation; Mass events in Insights. Observer API now: `ObservedTypes` (multiple) + `EMassObservedOperationFlags` (incl. `CreateEntity`/`DestroyEntity`); old single `ObservedType`/`Operation` (Add/Remove) **deprecated**. 5.8 batch element APIs renamed (`AddElementToEntities` → `BatchAddElementToEntities` family). |

### §4.2 Fragments, tags, shared fragments

```cpp
// Per-entity fragment (each entity owns one):
USTRUCT()
struct FOwnerUnitFragment : public FMassFragment
{
    GENERATED_BODY()
    UPROPERTY() FUnitHandle Owner;      // §5 Task 3 defines FUnitHandle (arch §2.3)
    UPROPERTY() int32  RosterIndex = INDEX_NONE;
};

// Shared fragment (one instance shared by many entities — content-hash deduped):
USTRUCT()
struct FUnitStatsSharedFragment : public FMassSharedFragment
{
    GENERATED_BODY()
    // ADR-14 / review E1: FIRST field is the uniqueness anchor. Mass dedups shared
    // fragments by content hash; two units with identical stats would otherwise
    // silently share one instance and cross-contaminate on in-place mutation.
    UPROPERTY() FUnitHandle OwnerUnit;
    UPROPERTY() float SpeedMult = 1.f;
    UPROPERTY() float MeleeAttackMult = 1.f;
    UPROPERTY() float MeleeDefenseMult = 1.f;
    UPROPERTY() float RangedAttackMult = 1.f;
    UPROPERTY() float BlockMult = 1.f;
    UPROPERTY() float FatigueAttackMult = 1.f;
    UPROPERTY() float FatigueDefenseMult = 1.f;
    UPROPERTY() float ArmorAdd = 0.f;
    UPROPERTY() float AccuracyMult = 1.f;
    UPROPERTY() float MoraleBonus = 0.f;
};

// Tag (empty UScriptStruct — never hold members). Bases come from MassCore:
// #include "Mass/EntityElementTypes.h" (audit C7).
USTRUCT()
struct FInfantryTag : public FMassTag { GENERATED_BODY() };
```

**Shared-fragment creation with a custom hash** ⚠️*pre-verified semantics*: "Shared fragments must be CRC hashable, otherwise you may not actually create a new instance when you call `GetOrCreateSharedFragmentByHash`. You can pass in your own hash with `GetOrCreateSharedFragmentByHash`, which helps if you prefer to control what makes each one unique" (MassSample §4.2.1 — this is the E1 citation). ADR-14 pattern:

```cpp
FUnitStatsSharedFragment Payload;
Payload.OwnerUnit = UnitHandle;                 // uniqueness anchor
Payload.SpeedMult = Snapshot.SpeedMult;         // ...fill from registry snapshot
// Audit C8: GetOrCreateSharedFragmentByHash is PRIVATE in 5.8. The anchor
// makes the payload value-distinct, so the public routes both yield per-unit
// instances: (a) EntityManager.GetOrCreateSharedFragment<FUnitStatsSharedFragment>(Payload)
// (content-CRC create), or (b) archetype attach via
// FMassArchetypeSharedFragmentValues::AddSharedFragment(FSharedStruct) + BatchCreateEntities
// (the Phase 0 spawn route, Task 6).
FSharedStruct Shared = EntityManager.GetOrCreateSharedFragment<FUnitStatsSharedFragment>(Payload);
```

**Two creation routes to audit in Task 2** (both must end with the fragment instance attached to the unit's entities):

- **Route A — archetype-first** ⚠️: build `FMassArchetypeCompositionDescriptor` (fragments+tags) and `FMassArchetypeSharedFragmentValues` (add the shared fragment handle), `EntityManager.CreateArchetype(Desc, Params)`, then `BatchCreateEntities` (optionally `BatchReserveEntities`) into that archetype. Reference: public 5.8 quickstart uses exactly `UMassEntitySubsystem::GetMutableEntityManager()` → `CreateArchetype` → `FMassArchetypeSharedFragmentValues` → `BatchCreateEntities`.
- **Route B — spawner/template** ⚠️: `UMassEntityConfigAsset` → entity template → `UMassSpawnerSubsystem::SpawnEntities` (5.8 template-based; pre-5.8 `SpawnEntitiesFromConfig` obsolete — review E4). Audit the exact 5.8 overload set (the subsystem has two `SpawnEntities` methods per 5.8-era forum confirmation).

**Sparse (virtual) fragments/tags — 5.8** ⚠️*pre-verified*: optional fragments and tags can be added/removed **without triggering an archetype change**; official guidance: regular fragments for data every entity of the archetype has, sparse ones for "rarely present, frequently toggled data". `FDeadTag` is the textbook case (roadmap R1 / Open Decision 7 → ADR-15 in Task 9; decided — adopt, §3.1 A3).

**Later mutation of a shared fragment**: processors declare `AddSharedRequirement<FUnitStatsSharedFragment>(EMassFragmentAccess::ReadWrite)` and write through `Context.GetMutableSharedFragment<FUnitStatsSharedFragment>()` per chunk ⚠️*pre-verified accessor*. Direct out-of-query mutation path (registry-held `FSharedStruct` → `GetMutable<T>()`) — audit existence in Task 2; UnitSync (Task 8) does **not** need it if it uses the query route.

### §4.3 Processors (5.8 signatures)

⚠️*pre-verified* against 5.8 public sources — the single most outdated area in older tutorials:

```cpp
UCLASS()
class UUnitSyncProcessor : public UMassProcessor
{
    GENERATED_BODY()
public:
    UUnitSyncProcessor();
protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
    FMassEntityQuery SyncQuery;
};

UUnitSyncProcessor::UUnitSyncProcessor()
    : SyncQuery(*this)                                  // 5.6+: ctor registers the query
{
    bAutoRegisterWithProcessingPhases = true;           // auto-instantiated, runs every frame
    SetProcessingPhase(EMassProcessingPhase::PrePhysics);
    ExecutionOrder.ExecuteInGroup = TEXT("Battle");     // custom group; built-ins in MassCommonTypes.h
    // ExecutionOrder.ExecuteAfter.Add(TEXT("..."));    // string-based ordering
    ExecutionFlags = (int32)EProcessorExecutionFlags::AllWorldModes;  // see note below
    bRequiresGameThreadExecution = true;                // UnitSync is game-thread (arch §4 row 0)
}

void UUnitSyncProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    SyncQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
    SyncQuery.AddSharedRequirement<FUnitStatsSharedFragment>(EMassFragmentAccess::ReadWrite);
}

void UUnitSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    SyncQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
    {
        auto Owners   = Context.GetFragmentView<FOwnerUnitFragment>();
        auto& Stats   = Context.GetMutableSharedFragment<FUnitStatsSharedFragment>();
        // per-owner dirty check against the registry, write in place, early-continue
        for (int32 i = 0; i < Context.GetNumEntities(); ++i) { /* ... */ }
    });
}
```

Notes verified from public sources:
- All processors auto-register and default to `EMassProcessingPhase::PrePhysics`; phases map to tick groups (PrePhysics→TG_PrePhysics ... FrameEnd→TG_LastDemotable).
- **ExecutionFlags**: `Standalone|Server|Client|Editor|EditorWorld` (+ combos `AllNetModes`, `AllWorldModes`, `All`). Default excludes EditorWorld — **explicitly set `AllWorldModes`** so Phase 0 structure probes work in editor worlds; PIE/standalone runs are unaffected.
- 5.8's scheduler derives parallelism from query access declarations — `ReadOnly` vs `ReadWrite` correctness is load-bearing (arch §4 "Threading rules").
- Optional modern API ⚠️: `UE::Mass::FQueryExecutor` (`AutoExecuteQuery = Executor;`) — official page "Simplified Mass Processor/Query API (Query Executor)". Phase 0 uses the classic style above (better reference coverage); note the option in the audit.

### §4.4 Query requirement & accessor cheat table

| Requirement | Call (5.8) |
|---|---|
| Fragment, R/W | `Query.AddRequirement<FFoo>(EMassFragmentAccess::ReadWrite)` |
| Presence | second arg `EMassFragmentPresence::All / Any / None / Optional` |
| Tag | `Query.AddTagRequirement<FTag>(EMassFragmentPresence::All)` |
| Shared fragment | `Query.AddSharedRequirement<FFoo>(EMassFragmentAccess::ReadWrite)` |
| World subsystem | `Query.AddSubsystemRequirement<USub>(EMassFragmentAccess::ReadOnly)` |
| Processor-level subsystem | `ProcessorRequirements.AddSubsystemRequirement<USub>(...)` (accessed in `Execute` outside query scope) |

Chunk-lambda accessors: `GetFragmentView<T>()` / `GetMutableFragmentView<T>()` (arrays), `GetConstSharedFragment<T>()` / `GetMutableSharedFragment<T>()`, `GetSubsystemChecked<T>(World)` / `GetMutableSubsystemChecked<T>(World)`, `GetChunkFragment<T>()` for chunk fragments. Tags have no accessors (no data). ⚠️*pre-verified* (MassSample §4.7 tables).

### §4.5 Command buffers & the E5 flush contract

- Inside processors: `Context.Defer().AddFragment<T>(E)` / `.RemoveFragment<T>(E)` / `.AddTag<T>(E)` / `.RemoveTag<T>(E)` / `.DestroyEntity(E)`; long form `PushCommand<FMassCommand...>`. ⚠️*pre-verified examples for Add/RemoveFragment*.
- Outside processors (game thread, not mid-processing): guard with `EntityManager.IsProcessing()`; if true, use `EntityManager.Defer()...`, else direct calls. Deferred create from outside: `EntityManager.Defer().PushCommand<FMassDeferredCreateCommand>(lambda)` ⚠️.
- **Flush contract (review E5) -- RESOLVED by Task 2 audit**: 5.8.4 flushes (a) at the end of each processor's processing scope (MassExecutor.cpp:153 / MassProcessingQueue.cpp:317, observers unlocked pre-flush) and (b) at processing-phase end for the manager buffer (MassProcessingPhaseManager.cpp:664). T0.5 asserts **directly post-Death-processor** semantics: deferred commands are applied before the next processor in the same phase observes state.
- 5.8 renamed batch element commands (`AddElementToEntities` → `BatchAddElementToEntities` family) — audit if you touch them directly (you mostly shouldn't; use `Defer()`).

### §4.6 Observers & Signals (exact 5.8 signatures)

**Signals** ⚠️*pre-verified full API* (module MassSignals, header `/Engine/Source/Runtime/Mass/MassSignals/Public/MassSignalSubsystem.h`):

```cpp
void UMassSignalSubsystem::SignalEntity(FName SignalName, const FMassEntityHandle Entity);
void SignalEntityDeferred(FMassExecutionContext& Context, FName SignalName, const FMassEntityHandle Entity);
void SignalEntities(FName, TConstArrayView<FMassEntityHandle>);
void SignalEntitiesDeferred(FMassExecutionContext&, FName, TConstArrayView<FMassEntityHandle>);
void DelaySignalEntity(FName, const FMassEntityHandle, const float DelayInSeconds);  // + Deferred variant
UE::Mass::FSignalDelegate& GetSignalDelegateByName(FName);
```

Receiver processor: subclass `UMassSignalProcessorBase`, call `SubscribeToSignal(Subsystem, SignalName)` and override `SignalEntities(FMassEntityManager&, FMassExecutionContext&, FMassSignalNameLookup&)` (5.8 shape — audit C2: no ConfigureSignals/SignalReceiveDelegate exists; optional `OnSignalReceived(FName, TConstArrayView<FMassEntityHandle>)`). **Signals carry only a name + one entity handle**: the handler reads `FOwnerUnitFragment` from the entity to recover the owning unit. The death chain (Task 9) is therefore:

```
kill (deferred tag add) → observer on FDeadTag add fires during flush
  → SignalEntityDeferred("Battle.UnitDeath", Entity)
  → receiver processor (game thread) reads FOwnerUnitFragment
  → registry roster update → SendGameplayEventToActor(UnitActor, "Battle.Unit.Died", ...)
```

**Observers** ⚠️: `UMassObserverProcessor` subclasses fire on structural operations at flush time. 5.8 API: `ObservedTypes` (multiple types) + `EMassObservedOperationFlags` (`Add`/`Remove`/`CreateEntity`/`DestroyEntity`); the pre-5.8 single `ObservedType`+`Operation` pair is deprecated. Audit `MassObserverProcessor.h` in Task 2 and record the exact registration shape (tag discovery from the cached entity query vs explicit `ObservedTypes`).

### §4.7 World subsystems usable by Mass (registry pattern)

`UUnitRegistrySubsystem` must be readable by worker processors from Phase 1 — bake the discipline now ⚠️*pre-verified pattern* (MassSample §4.4):

```cpp
UCLASS()
class UUnitRegistrySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    // Phase 0: game-thread API only. Phase 1 adds scoped read access for workers.
    // Phase 1+ worker reads:
    // UE_MT_SCOPED_READ_ACCESS(AccessDetector);
private:
    UE_MT_DECLARE_RW_ACCESS_DETECTOR(AccessDetector);
    TMap<FUnitHandle, FUnitRuntimeState> Units;   // plain structs, no UObject*
    // free-list + serial counters per arch §2.3
};

template<>
struct TMassExternalSubsystemTraits<UUnitRegistrySubsystem> final
{
    enum { GameThreadOnly = false, ThreadSafeWrite = false };
};
```

(Members are `GameThreadOnly`/`ThreadSafeWrite` -- audit C3; header is MassCore `Mass/ExternalSubsystemTraits.h`, the old `MassExternalSubsystemTraits.h` is a 5.8 deprecated shim. If traits cannot live in the same header, use the `MassGameplayExternalTraits.h` separate-header pattern.)

### §4.8 Spawning & entity creation

- **Subsystem access**: `World->GetSubsystem<UMassEntitySubsystem>()` → `GetMutableEntityManager()` ⚠️*pre-verified*.
- **Structural-change guard**: never mutate archetypes while Mass is processing — check `EntityManager.IsProcessing()`; defer if true (§4.5).
- **Route A (C++ archetype-first — primary for Phase 0 spawn library)**: `CreateArchetype(FMassArchetypeCompositionDescriptor, FMassArchetypeCreationParams)` + `FMassArchetypeSharedFragmentValues` → `BatchReserveEntities` → `BatchCreateEntities` → fixup pass writes `FOwnerUnitFragment`/`FBaseStatsFragment` values ⚠️*pre-verified call names*.
- **Route B (config-driven)**: `UMassEntityConfigAsset` (MassSpawner) + traits (`UMassEntityTraitBase::BuildTemplate(FMassEntityTemplateBuildContext&, const UWorld&)`; `BuildContext.AddFragment<T>() / AddFragment_GetRef<T>() / AddTag<T>() / AddSharedFragment(FSharedStruct)` -- audit C6: `RequireFragment<T>` absent in 5.8) → `UMassSpawnerSubsystem::SpawnEntities` (5.8 template-based overloads — audit exact set; E4). T0.3 requires the config asset path to exist and resolve; Task 6 authors the assets via ExecBridge (§4.11) rather than by hand in the editor (decided — §3.1 A5).
- **Kill (test path)**: defer tag add, see §4.5/Task 9.

### §4.9 GAS essentials (stable API, low audit risk)

```cpp
UCLASS()
class UUnitAttributeSet : public UAttributeSet
{
    GENERATED_BODY()
public:
    // Closed set — arch §10.1. Full GAMEPLAYATTRIBUTE_ macros per attribute:
    UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData Morale;
    ATTRIBUTE_ACCESSORS(UUnitAttributeSet, Morale)
    // ... MeleeAttackMod, MeleeDefenseMod, RangedAttackMod, SpeedMod,
    //     BlockChanceMod, Fatigue, Experience, ArmourUpgrade, OrderLatency, AmmoPool
};
```

- Wire-up: `ASC->InitAbilityActorInfo(this, this)` in `PostInitializeComponents`; then for each attribute in a static list `{ GetMoraleAttribute(), GetMeleeAttackModAttribute(), ... }` register `ASC->GetGameplayAttributeValueChangeDelegate(Attr).AddUObject(this, &AUnitActor::OnAttributeChanged)` → handler sets the registry dirty flag for the unit (arch §5.1; one frame of latency accepted).
- Test drive for T0.4 — **no GE asset needed**: `ASC->SetNumericAttributeBase(UUnitAttributeSet::GetMeleeAttackModAttribute(), Value);` fires the same delegate path.
- Up-path event (Task 9): `AbilitySystemBlueprintLibrary::SendGameplayEventToActor(UnitActor, FGameplayTag("Battle.Unit.Died"), FGameplayEventData())` (approx. — audit exact signature).

### §4.10 Automation framework essentials

```cpp
#include "Misc/AutomationTest.h"

// No-world / created-world unit test (per-commit tier):
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegistryLifecycle, "Battle.Unit.Registry.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FRegistryLifecycle::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);   // world subsystems initialize
    UUnitRegistrySubsystem* Reg = World->GetSubsystem<UUnitRegistrySubsystem>();
    TestNotNull(TEXT("registry"), Reg);
    // ... assertions; AddError() fails the test; TestEqual/TestTrue helpers
    World->DestroyWorld();
    return true;
}
```

- `FAutomationEditorCommonUtils::CreateNewMap()` is the editor-map alternative but requires an `UnrealEd` dependency — **use `UWorld::CreateWorld(EWorldType::Game, false)` instead** to keep the runtime plugin module clean.
- Editor-context tests run in the editor session → verify through `AutomationTestToolset` (§2). The existing `Battle.Smoke.PluginLoaded` in `MassBattleSmokeTest.cpp` is the template for flags/naming.
- Functional tests (T0.4/T0.5) tick the world → `AFunctionalTest` subclass on a minimal map `L_MassBattle_T0_Functional`, driven through `UFunctionalTestingManager` (the `AutomationTestToolset` path handles this). Fallback if PIE automation is flaky through MCP: run the functional tier headless via the Task 1 CI script and read the log/report — the roadmap's `-ExecCmds="Automation RunTests Battle.Func" -TestExit="Automation Test Queue Empty"` line exists for exactly this.
- Expected-error whitelist: `AddExpectedError(...)`.
- Fixed-step: `FApp::SetUseFixedTimeStep(true); FApp::SetFixedDeltaTime(1.0/60.0);` in test setup (audit C4: `SetFixedTimeStepValue` does not exist in 5.8; read-back is `FApp::UseFixedTimeStep()` / `GetFixedDeltaTime()`); T0.9 helper in Task 11 reads **active** values not just the CVars it set.

### §4.11 ExecBridge pattern (C++ console commands as the MCP bridge)

Anything the MCP toolsets cannot do (build configs, spawn rosters, kill entities, dump registry state) becomes an `FAutoConsoleCommandWithWorldAndArgs` that logs structured, grep-able output:

```cpp
#include "Containers/UnrealString.h"
#include "HAL/IConsoleManager.h"

namespace BattleExec
{
    static void SpawnRoster(const TArray<FString>& Args, UWorld* World)
    {
        // Args: <Type=Peasant|Sergeant> <Count> <X> <Y>
        // ... build unit actor + entities via the spawn library ...
        UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SpawnRoster Unit=%u:%u Entities=%d"),
               UnitHandle.Index, UnitHandle.Serial, Count);
    }
}
static FAutoConsoleCommandWithWorldAndArgs GBattleSpawnRoster(
    TEXT("Battle.SpawnRoster"),
    TEXT("<Type> <Count> <X> <Y> — spawns a unit actor + roster; logs EXEC_RESULT lines"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::SpawnRoster));
```

Phase 0 command set (implemented across Tasks 4–9): `Battle.SpawnRoster`, `Battle.BuildT0Configs`, `Battle.SetAttr <Unit> <Attr> <Value>`, `Battle.KillEntity <Unit> <RosterIndex>`, `Battle.DumpRegistry`, `Battle.DumpSharedFrag <Unit>`, `Battle.StateHash`. Convention: every command emits at least one `EXEC_RESULT <Name> ...` line → verified via `LogsToolset.GetLogEntries(category:"", pattern:"EXEC_RESULT")`. Commands must be safe in the editor world (structure-only probes); anything needing sim ticks runs as a functional test instead (§4.10 split).

### §4.12 CI stack (R8) — verified building blocks

| Block | Verified facts |
|---|---|
| Headless automation run | `UnrealEditor-Cmd.exe "<uproject>" -game -unattended -nopause -nosplash -nullrhi -log=CI.log -ExecCmds="Automation RunTests Battle.Unit+Battle.Data+Battle.Smoke; Quit" -TestExit="Automation Test Queue Empty"` — long-standing documented pattern (Epic forums/StackOverflow; roadmap B.0 already mandates the same flags). Script parses the log for the pass/fail summary rather than trusting exit codes. Audit C5: 5.8 renames it to `-ReportExportPath=<dir>` (writes index.json, UTF-8 BOM); the old name still parses with a deprecation warning. |
| Gauntlet | Engine framework "to run sessions of projects and validate results"; C# driver under `Engine/Source/Programs/AutomationTool/Gauntlet`, invoked via RunUAT (`RunGauntlet`); game-side controllers are `UGauntletTestController` subclasses in the **Gauntlet plugin** (`-gauntlet=<ControllerName>` enables one). Official 5.8 doc pages: "Gauntlet Automation Framework in Unreal Engine" → Overview / Run Gauntlet Tests / Gauntlet Controller. Phase 0 ships one stub controller (`UBattleSmokeController`: wait N s, assert no errors, `EndTest`) purely to prove the packaged-nightly loop end-to-end. ⚠️ audit: plugin location/state in 5.8 (`Engine/Plugins/...Gauntlet`) and the exact RunUAT verb/args. |
| Orchestration host | Roadmap R8 names "Horde / Jenkins / hosted CI". Horde in 5.8 is aimed at build farms (release notes: Horde analytics/perf reporting) — heavier than a single-machine project needs. Jenkins LTS controller+agent on the reference machine (or initially Windows Task Scheduler as a stopgap) runs: per-commit gate (build + unit/data/smoke) and the nightly job (package Win64 via `RunUAT BuildCookRun`, then Gauntlet stub, then baseline compare). The Jenkinsfile skeleton in Task 1 is a starting point, not doctrine — Task 1's ADR-16 records the host decision and why. |
| Baseline JSON storage | `Baselines/perf/<machine-tag>/<gate>.json`, schema `{ machine, engineCL, date, metrics{...} }`; `Tools/CI/compare_baseline.py` fails on regression beyond tolerance. Phase 0 seeds the loop with two trivial-but-real metrics (80-entity spawn time; smoke test duration) so storage+compare is exercised before real perf gates arrive in Phase 2 (T2.9). Every file embeds the reference-machine spec (R10). |
| Zen/server caution (5.8) | 5.8 release notes: for automated/build processes, set `LimitProcessLifetime=false` in `[Zen.AutoLaunch]` of `DefaultEngine.ini` so Zenserver doesn't churn during CI runs. |

### §4.13 Sources for §4 (full list in Part III)

Primary: MassSample README (Megafunk, updated to 5.8) · vrealmatic Mass Framework wiki (explicitly updated to UE 5.8) · Epic 5.8 release notes · Epic API docs (`UMassSignalSubsystem`) · Epic "Simplified Mass Processor" page · Gauntlet doc pages · nukethebees query-API quickstart · Epic forums (spawner usage, Jan 2026 threads).


---

# Part C — Research sources (verified 2026-10-24)

> Sources consulted during the Phase 0 brief's research pass, kept beside the digest they verified.

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
| Epic 5.8 docs — "Mass Avoidance — Overview" (Experimental warning, force-based description) + MassMovement API page | Phase 1 brief §4 digest: avoidance processor/fragment inventory, Experimental status, debug categories (pre-verification; audited on-machine 2026-10-24 → Part A §14) |
| vrealmatic Mass wiki — Steering page (5.8-updated) | goal-force formula + `SteerK = 1/ReactionTime`, trait/fragment shapes (pre-verification; C14 corrected the reference-velocity term on-machine) |
| Community MassGameplay source-analysis repo | ApplyForce `DesiredVelocity += Force*DT` integration claim (pre-verification; confirmed on-machine with C16/C20 refinements) |
| `Docs/phases/pre-phase-0/report.md` §2/§4 (on-machine) | all [machine] facts in §1/§2 |
