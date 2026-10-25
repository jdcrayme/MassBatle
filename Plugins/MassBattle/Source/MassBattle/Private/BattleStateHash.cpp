#include "BattleStateHash.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleRegistry.h"
#include "Hash/CityHash.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "MassCommonFragments.h"
#include "Engine/World.h"

int64 FBattleStateHash::Quantize2(float Value)
{
	// 1e-2 precision, symmetric rounding; deterministic for identical floats.
	return static_cast<int64>(FMath::RoundToDouble(static_cast<double>(Value) * 100.0));
}

uint64 FBattleStateHash::Compute(UWorld* World)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !EntitySubsystem)
	{
		return 0;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	// Deterministic byte stream: every term appended in a fixed order.
	TArray<uint8> Bytes;
	auto Append = [&Bytes](const void* Data, int32 Size)
	{
		Bytes.Append(static_cast<const uint8*>(Data), Size);
	};
	auto AppendU64 = [&Append](uint64 V)
	{
		Append(&V, sizeof(V));
	};

	// 1. Registry bookkeeping (sorted handles -> serials -> roster sizes;
	// dirty flags excluded -- transient pipeline state, audit M4).
	uint64 Bookkeeping = Registry->ComputeBookkeepingHash();
	AppendU64(Bookkeeping);

	// 2. Per-unit entity state, in roster order: HP (quantized 1e-2), dead flag,
	// transform (quantized 1 cm; yaw at 0.1 deg -- phase-1 ground rule 7).
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		if (!Roster)
		{
			continue;
		}
		for (const FMassEntityHandle& Entity : *Roster)
		{
			const FMassEntityView View(EntityManager, Entity);
			if (const FHealthFragment* Health = View.GetFragmentDataPtr<FHealthFragment>())
			{
				AppendU64(static_cast<uint64>(Quantize2(Health->CurrentHP)));
				AppendU64(static_cast<uint64>(Quantize2(Health->QueuedDamage)));
			}
			const bool bDead = View.HasTag<FDeadTag>() ? 1 : 0;
			AppendU64(bDead);
			if (const FTransformFragment* Transform = View.GetFragmentDataPtr<FTransformFragment>())
			{
				const FVector Pos = Transform->GetTransform().GetTranslation();
				// 1 cm quantization: UE units are centimeters, so round to
				// integer units; symmetric rounding stays deterministic.
				AppendU64(static_cast<uint64>(FMath::RoundToDouble(Pos.X)));
				AppendU64(static_cast<uint64>(FMath::RoundToDouble(Pos.Y)));
				AppendU64(static_cast<uint64>(FMath::RoundToDouble(Pos.Z)));
				const float Yaw = Transform->GetTransform().Rotator().Yaw;
				AppendU64(static_cast<uint64>(FMath::RoundToDouble(static_cast<double>(Yaw) * 10.0)));
			}
		}
	}

	// 3. Archetype count (unbounded-growth watch, B.4). Debug-only engine API:
	// absent in Shipping (WITH_MASSENTITY_DEBUG off) -- folds 0 there, keeping
	// the term position-stable without shipping the debug build.
	const int32 ArchetypeCount =
#if WITH_MASSENTITY_DEBUG
		EntityManager.DebugGetArchetypesCount();
#else
		0;
#endif
	AppendU64(static_cast<uint64>(ArchetypeCount));

	// 4. Per-unit shared stat values (quantized), in the same sorted order.
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		if (!Roster || Roster->Num() == 0)
		{
			continue;
		}
		const FMassEntityView View(EntityManager, (*Roster)[0]);
		if (const FUnitStatsSharedFragment* Stats = EntityManager.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*Roster)[0]))
		{
			AppendU64(static_cast<uint64>(Quantize2(Stats->SpeedMult)));
			AppendU64(static_cast<uint64>(Quantize2(Stats->MeleeAttackMult)));
			AppendU64(static_cast<uint64>(Quantize2(Stats->MeleeDefenseMult)));
			AppendU64(static_cast<uint64>(Quantize2(Stats->MoraleBonus)));
		}
		// 5. Formation movement state (phase-1 ground rule 7): anchor at 1 cm,
		// facing at 0.1 deg, speed limit + spacing scale at 1e-2.
		if (const FUnitFormationSharedFragment* Formation = EntityManager.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]))
		{
			AppendU64(static_cast<uint64>(FMath::RoundToDouble(static_cast<double>(Formation->AnchorPos.X))));
			AppendU64(static_cast<uint64>(FMath::RoundToDouble(static_cast<double>(Formation->AnchorPos.Y))));
			AppendU64(static_cast<uint64>(FMath::RoundToDouble(static_cast<double>(Formation->FacingYaw) * 10.0)));
			AppendU64(static_cast<uint64>(Quantize2(Formation->SpeedLimit)));
			AppendU64(static_cast<uint64>(Quantize2(Formation->SpacingScale)));
			AppendU64(static_cast<uint64>(Formation->Generation));
		}
	}

	return CityHash64(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<uint32>(Bytes.Num()));
}
