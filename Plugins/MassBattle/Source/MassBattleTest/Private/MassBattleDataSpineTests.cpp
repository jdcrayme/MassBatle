#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.Mass.* -- API-level probes of the Mass data spine (T0.8 probe half).
// The spawn-level T0.8 assertions (spawned units -> distinct fragments, A/B
// isolation) live in the functional sync test (Task 8); this file proves the
// shared-fragment creation semantics the spawn path is built on.
//
// Audit C8: `GetOrCreateSharedFragmentByHash` is private in 5.8; the public
// create route is content-CRC (`GetOrCreateSharedFragment`). ADR-14 therefore
// guarantees per-unit uniqueness through the FUnitHandle anchor making each
// payload value-distinct -- asserted here directly.
#include "Misc/AutomationTest.h"

#include "BattleTypes.h"
#include "BattleFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "StructUtils/SharedStruct.h"

namespace
{
	// Identical stats for every call -- the E1 adversarial case: without the
	// handle anchor all 8 would dedup into one instance.
	FUnitStatsSharedFragment MakePayload(const FUnitHandle* OwnerUnit)
	{
		FUnitStatsSharedFragment Payload;
		if (OwnerUnit)
		{
			Payload.OwnerUnit = *OwnerUnit;         // ADR-14 anchor
		}
		Payload.SpeedMult = 1.0f;                   // identical stats below
		Payload.MeleeAttackMult = 1.0f;
		Payload.MeleeDefenseMult = 1.0f;
		Payload.RangedAttackMult = 1.0f;
		Payload.BlockMult = 1.0f;
		Payload.FatigueAttackMult = 1.0f;
		Payload.FatigueDefenseMult = 1.0f;
		Payload.ArmorAdd = 0.0f;
		Payload.AccuracyMult = 1.0f;
		Payload.MoraleBonus = 0.0f;
		return Payload;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMassBattle_SharedFragUniqueness,
	"Battle.Unit.Mass.SharedFragUniqueness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassBattle_SharedFragUniqueness::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("world created"), World);
	if (!World)
	{
		return false;
	}

	UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("MassEntitySubsystem"), EntitySubsystem);
	if (!EntitySubsystem)
	{
		World->DestroyWorld(false);
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	// -- E1 hazard demonstration: content-hash dedup WITHOUT the anchor --
	{
		const FSharedStruct& A = EntityManager.GetOrCreateSharedFragment<FUnitStatsSharedFragment>(MakePayload(nullptr));
		const FSharedStruct& B = EntityManager.GetOrCreateSharedFragment<FUnitStatsSharedFragment>(MakePayload(nullptr));
		TestEqual(TEXT("anchor-less identical payloads dedup to ONE instance (the E1 hazard)"),
			A.GetMemory(), B.GetMemory());
	}

	// -- ADR-14: 8 anchored payloads -> 8 distinct instances --
	constexpr int32 NumUnits = 8;
	TArray<FSharedStruct> Instances;
	for (int32 i = 0; i < NumUnits; ++i)
	{
		FUnitHandle Handle;
		Handle.Index = static_cast<uint32>(i);
		Handle.Serial = 1000u + static_cast<uint32>(i);
		const FSharedStruct& Instance =
			EntityManager.GetOrCreateSharedFragment<FUnitStatsSharedFragment>(MakePayload(&Handle));
		Instances.Add(Instance);
	}

	for (int32 i = 0; i < NumUnits; ++i)
	{
		for (int32 j = i + 1; j < NumUnits; ++j)
		{
			TestNotEqual(FString::Printf(TEXT("instances %d vs %d are distinct"), i, j),
				Instances[i].GetMemory(), Instances[j].GetMemory());
		}
	}

	// Anchors survive: each instance carries its own unit handle.
	for (int32 i = 0; i < NumUnits; ++i)
	{
		const FUnitStatsSharedFragment& Frag = Instances[i].Get<FUnitStatsSharedFragment>();
		TestEqual(FString::Printf(TEXT("instance %d anchor index"), i), Frag.OwnerUnit.Index, static_cast<uint32>(i));
		TestEqual(FString::Printf(TEXT("instance %d anchor serial"), i), Frag.OwnerUnit.Serial, 1000u + static_cast<uint32>(i));
	}

	// Idempotent recreate: same anchored payload returns the same instance.
	{
		FUnitHandle Handle;
		Handle.Index = 2;
		Handle.Serial = 1002;
		const FSharedStruct& Dup = EntityManager.GetOrCreateSharedFragment<FUnitStatsSharedFragment>(MakePayload(&Handle));
		TestEqual(TEXT("same-payload recreate returns the same instance"), Dup.GetMemory(), Instances[2].GetMemory());
	}

	// In-place mutation of one instance leaves the others untouched.
	{
		FUnitStatsSharedFragment* Mutable3 = reinterpret_cast<FUnitStatsSharedFragment*>(Instances[3].GetMemory());
		Mutable3->SpeedMult = 0.5f;

		for (int32 i = 0; i < NumUnits; ++i)
		{
			const FUnitStatsSharedFragment& Frag = Instances[i].Get<FUnitStatsSharedFragment>();
			if (i == 3)
			{
				TestEqual(TEXT("mutated instance sees new value"), Frag.SpeedMult, 0.5f);
			}
			else
			{
				TestEqual(FString::Printf(TEXT("instance %d unaffected by mutation of 3"), i), Frag.SpeedMult, 1.0f);
			}
		}
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
