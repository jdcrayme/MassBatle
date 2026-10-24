# Pinned-API audit — UE 5.8.4 (roadmap I6, BRIEF §5 Task 2)

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

---

### Discrepancies vs BRIEF §4 — all resolved

C1–C7 (§0 above). None of them change the Phase 0 architecture; C2 (signal receiver shape) and C3 (traits members) change Task 4/9 code shape exactly as recorded here. The brief's §4 lines were corrected in place at Task 2 time; no other ⚠️ remains unresolved.
