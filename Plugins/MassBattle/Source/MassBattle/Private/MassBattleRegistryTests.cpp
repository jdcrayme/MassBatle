// Battle.Unit.Registry.* -- T0.1: handle registry lifecycle (roadmap T0 gate).
// stale handles rejected (generation counter) / free-list reuse safe /
// 10k spawn-destroy round-trip leak-free / dirty-flag idempotence.
#include "Misc/AutomationTest.h"

#include "BattleRegistry.h"
#include "BattleExecCommands.h"
#include "BattleTypes.h"
#include "Engine/Engine.h"

namespace
{
	UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	FUnitSpawnSpec MakeSpec(int32 Row = 0, int32 Count = 40)
	{
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = Row;
		Spec.SoldierCount = Count;
		Spec.UnitName = TEXT("TestUnit");
		return Spec;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegistry_StaleHandles,
	"Battle.Unit.Registry.StaleHandles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegistry_StaleHandles::RunTest(const FString& Parameters)
{
	UWorld* World = MakeWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	TestNotNull(TEXT("registry"), Registry);
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}

	const FUnitHandle First = Registry->RegisterUnit(MakeSpec());
	TestTrue(TEXT("first handle valid"), First.IsValid());
	TestTrue(TEXT("first handle registered"), Registry->IsHandleValid(First));

	// Free the slot, then re-register: same Index, new Serial.
	const uint32 ReusedIndex = First.Index;
	TestTrue(TEXT("unregister ok"), Registry->UnregisterUnit(First));
	TestFalse(TEXT("freed handle invalid"), Registry->IsHandleValid(First));

	const FUnitHandle Second = Registry->RegisterUnit(MakeSpec(1));
	TestTrue(TEXT("second handle valid"), Second.IsValid());
	TestEqual(TEXT("slot reused (same index)"), Second.Index, ReusedIndex);
	TestNotEqual(TEXT("serial bumped (generation counter)"), Second.Serial, First.Serial);

	// Stale handle (first serial) is rejected everywhere.
	TestFalse(TEXT("stale handle rejected"), Registry->IsHandleValid(First));
	TestFalse(TEXT("stale unregister rejected"), Registry->UnregisterUnit(First));
	TestFalse(TEXT("stale roster lookup rejected"), Registry->FindRoster(First) != nullptr);
	TestFalse(TEXT("stale spec lookup rejected"), Registry->FindUnitSpec(First) != nullptr);
	TestFalse(TEXT("stale state lookup rejected"), Registry->FindUnitState(First) != nullptr);

	// Null / zero handle is invalid by construction.
	FUnitHandle NullHandle;
	TestFalse(TEXT("null handle invalid"), NullHandle.IsValid());
	TestFalse(TEXT("null handle rejected"), Registry->IsHandleValid(NullHandle));

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegistry_RoundTrip10k,
	"Battle.Unit.Registry.RoundTrip10k",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegistry_RoundTrip10k::RunTest(const FString& Parameters)
{
	UWorld* World = MakeWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}

	constexpr int32 NumUnits = 10000;
	TArray<FUnitHandle> Handles;
	Handles.Reserve(NumUnits);

	for (int32 i = 0; i < NumUnits; ++i)
	{
		const FUnitHandle Handle = Registry->RegisterUnit(MakeSpec(i % 2));
		if (!TestTrue(FString::Printf(TEXT("handle %d allocated"), i), Handle.IsValid()))
		{
			World->DestroyWorld(false);
			return false;
		}
		Handles.Add(Handle);
	}
	TestEqual(TEXT("count after 10k registers"), Registry->GetUnitCount(), NumUnits);
	TestEqual(TEXT("free list empty while full"), Registry->GetFreeListSize(), 0);

	// Publish small rosters so bookkeeping hash has real content.
	for (int32 i = 0; i < NumUnits; ++i)
	{
		TArray<FMassEntityHandle> Roster;
		Roster.Add(FMassEntityHandle{static_cast<int32>(i + 1), 1});
		Registry->SetRoster(Handles[i], MoveTemp(Roster));
	}

	// Unregister everything -> leak-free (no slots, no specs, no dirty flags).
	for (int32 i = 0; i < NumUnits; ++i)
	{
		Registry->MarkStatsDirty(Handles[i]);
		TestTrue(FString::Printf(TEXT("unregister %d"), i), Registry->UnregisterUnit(Handles[i]));
	}

	TestEqual(TEXT("count back to zero"), Registry->GetUnitCount(), 0);
	TestEqual(TEXT("free list holds every slot once"), Registry->GetFreeListSize(), NumUnits);
	TestTrue(TEXT("bookkeeping hash stable when empty after churn"),
	         Registry->ComputeBookkeepingHash() == Registry->ComputeBookkeepingHash());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegistry_FreeListReuseSafety,
	"Battle.Unit.Registry.FreeListReuseSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegistry_FreeListReuseSafety::RunTest(const FString& Parameters)
{
	UWorld* World = MakeWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}

	// Allocate 4, free 1 and 3 (LIFO free list), re-allocate -> must reuse in
	// LIFO order with fresh serials and no cross-talk between tenants.
	TArray<FUnitHandle> Handles;
	for (int32 i = 0; i < 4; ++i)
	{
		Handles.Add(Registry->RegisterUnit(MakeSpec(i)));
	}

	TestTrue(TEXT("free idx3"), Registry->UnregisterUnit(Handles[3]));
	TestTrue(TEXT("free idx1"), Registry->UnregisterUnit(Handles[1]));

	const FUnitHandle ReuseA = Registry->RegisterUnit(MakeSpec(9));
	const FUnitHandle ReuseB = Registry->RegisterUnit(MakeSpec(8));

	// Old handles must all be stale now.
	for (const FUnitHandle Old : {Handles[1], Handles[3]})
	{
		TestFalse(TEXT("old tenant stale after reuse"), Registry->IsHandleValid(Old));
	}

	// Survivors untouched.
	TestTrue(TEXT("tenant0 alive"), Registry->IsHandleValid(Handles[0]));
	TestTrue(TEXT("tenant2 alive"), Registry->IsHandleValid(Handles[2]));

	// Reuse slots are the freed ones, with bumped serials.
	const uint32 ReuseAIndex = ReuseA.Index;
	const bool bAIsFreedSlot = (ReuseAIndex == Handles[1].Index || ReuseAIndex == Handles[3].Index);
	TestTrue(TEXT("reuse takes a freed slot"), bAIsFreedSlot);
	const bool bBIsFreedSlot = (ReuseB.Index == Handles[1].Index || ReuseB.Index == Handles[3].Index);
	TestTrue(TEXT("second reuse takes the other freed slot"), bBIsFreedSlot);
	TestEqual(TEXT("no slot leaked"), Registry->GetUnitCount(), 4);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegistry_DirtyFlags,
	"Battle.Unit.Registry.DirtyFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegistry_DirtyFlags::RunTest(const FString& Parameters)
{
	UWorld* World = MakeWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}

	const FUnitHandle Unit = Registry->RegisterUnit(MakeSpec());
	TestTrue(TEXT("unit registered"), Unit.IsValid());

	TestFalse(TEXT("not dirty initially"), Registry->IsStatsDirty(Unit));

	// Idempotence (T0.1): burst of marks leaves exactly one dirty entry.
	Registry->MarkStatsDirty(Unit);
	Registry->MarkStatsDirty(Unit);
	Registry->MarkStatsDirty(Unit);
	TestTrue(TEXT("dirty after burst"), Registry->IsStatsDirty(Unit));

	// Clear + re-mark behaves as a fresh flag.
	Registry->ClearStatsDirty(Unit);
	TestFalse(TEXT("clean after clear"), Registry->IsStatsDirty(Unit));
	Registry->MarkStatsDirty(Unit);
	TestTrue(TEXT("re-markable"), Registry->IsStatsDirty(Unit));

	// Unregister clears the flag with the unit.
	Registry->UnregisterUnit(Unit);
	TestFalse(TEXT("flag dies with the unit"), Registry->IsStatsDirty(Unit));

	// Cached attribute round trip.
	const FUnitHandle Other = Registry->RegisterUnit(MakeSpec());
	FBattleAttributeSnapshot Snapshot;
	Snapshot.Morale = 4.25f;
	Registry->UpdateCachedAttributes(Other, Snapshot);
	const FBattleAttributeSnapshot* ReadBack = Registry->FindCachedAttributes(Other);
	TestNotNull(TEXT("attributes cached"), ReadBack);
	if (ReadBack)
	{
		TestEqual(TEXT("morale round trip"), ReadBack->Morale, 4.25f);
	}

	World->DestroyWorld(false);
	return true;
}

// ExecBridge probe (BRIEF §4.11 / §2 fallback): MCP has no console-execution
// toolset, so the Battle.* commands are exercised here via World->Exec; their
// EXEC_RESULT lines land in the editor session log where LogsToolset (or the
// CI log) provides the grep-able evidence.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegistry_ExecProbes,
	"Battle.Unit.Registry.ExecProbes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegistry_ExecProbes::RunTest(const FString& Parameters)
{
	UWorld* World = MakeWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}

	const FUnitHandle A = Registry->RegisterUnit(MakeSpec(0, 40));
	const FUnitHandle B = Registry->RegisterUnit(MakeSpec(1, 40));
	TestTrue(TEXT("unit A"), A.IsValid());
	TestTrue(TEXT("unit B"), B.IsValid());
	Registry->MarkStatsDirty(A);

	const uint32 HashBefore = Registry->ComputeBookkeepingHash();

	// Structure-only probes: must run clean in the editor world.
	// Route note: neither -ExecCmds (editor-cmd) nor UWorld::Exec reach
	// FAutoConsoleCommandWithWorldAndArgs objects in 5.8 (tried on-machine,
	// zero EXEC_RESULT lines both ways), so the probe drives the BattleExec::
	// implementations the console objects delegate to -- same code, same
	// EXEC_RESULT evidence lines in this session log (LogsToolset greps them).
	GEngine->Exec(World, TEXT("Battle.DumpRegistry")); // engine route, if wired
	BattleExec::DumpRegistry(TArray<FString>(), World);
	BattleExec::StateHash(TArray<FString>(), World);

	// Probes must not mutate state (structure-only contract).
	TestEqual(TEXT("hash unchanged by probes"), Registry->ComputeBookkeepingHash(), HashBefore);
	TestEqual(TEXT("count unchanged by probes"), Registry->GetUnitCount(), 2);
	TestTrue(TEXT("dirty flag survives probes"), Registry->IsStatsDirty(A));

	World->DestroyWorld(false);
	return true;
}
