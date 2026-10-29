#include "BattleStateHash.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleCombatFragments.h"
#include "BattleRangedFragments.h"
#include "BattleRegistry.h"
#include "BattleProjectileSubsystem.h"
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

uint64 FBattleStateHash::Compute(UWorld* World, TArray<uint64>* OutSectionHashes)
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
	// Section boundaries: when bisecting, each section's bytes are hashed
	// into OutSectionHashes at its end (the RC8 divergence bisector).
	auto EndSection = [OutSectionHashes, &Bytes]()
	{
		if (OutSectionHashes)
		{
			OutSectionHashes->Add(CityHash64(reinterpret_cast<const char*>(Bytes.GetData()),
				static_cast<uint32>(Bytes.Num())));
		}
	};

	// 1. Registry bookkeeping (sorted handles -> serials -> roster sizes;
	// dirty flags excluded -- transient pipeline state, audit M4).
	uint64 Bookkeeping = Registry->ComputeBookkeepingHash();
	AppendU64(Bookkeeping);
	EndSection();

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

	EndSection();
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
	EndSection();

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
			// 6. Garrison terms (spec §3.3, F9/R-G6): appended AFTER the
			// formation terms, position-stable, same sorted-handle pass. The
			// booked-range mirror contents are derivable from these terms +
			// the structure records (deterministic copies at Occupy), so the
			// identity terms suffice -- hashing 64 fixed slots per unit would
			// quadruple the hash input for zero drift coverage.
			AppendU64(static_cast<uint64>(Formation->GarrisonMode));
			AppendU64(static_cast<uint64>(Formation->GarrisonStructureId));
			AppendU64(static_cast<uint64>(Formation->GarrisonPhase));
			AppendU64(static_cast<uint64>(Formation->GarrisonSlotBase));
			AppendU64(static_cast<uint64>(Formation->GarrisonSlotCount));
		}
		// 7. Combat pair/charge terms (spec §3.4): appended AFTER the garrison
		// terms, same sorted-handle pass, per entity. Pair target handle,
		// quantized contact angle (1/255), pair state, attack timer (1e-2 s),
		// charge distance (1 cm) / impact speed (1e-2 m/s), mount HP. A fixed
		// term count per entity keeps the byte stream position-stable when a
		// fragment is absent (pre-combat spawn paths append zeros).
		for (const FMassEntityHandle& Entity : *Roster)
		{
			const FMassEntityView EntityView(EntityManager, Entity);
			if (const FCombatPairFragment* Pair = EntityView.GetFragmentDataPtr<FCombatPairFragment>())
			{
				// The pair target hashes as its CANONICAL reference --
				// (owner unit handle, roster index) -- never the raw
				// FMassEntityHandle: entity slots/generations differ across a
				// battle reset, and the RC8 two-run equality with REAL units
				// diverged on exactly that (measured 2026-10-29: the phase-2
				// gate hashed an empty world and never saw it). Zero-filled
				// when the target is invalid or unresolvable.
				FUnitHandle TargetUnit;
				int32 TargetRosterIndex = -1;
				if (Pair->Target.IsValid())
				{
					if (const FOwnerUnitFragment* TargetOwner =
						FMassEntityView(EntityManager, Pair->Target).GetFragmentDataPtr<FOwnerUnitFragment>())
					{
						TargetUnit = TargetOwner->Owner;
						TargetRosterIndex = TargetOwner->RosterIndex;
					}
				}
				AppendU64(TargetUnit.Index);
				AppendU64(TargetUnit.Serial);
				AppendU64(static_cast<uint64>(FMath::Max(0, TargetRosterIndex)));
				AppendU64(Pair->ContactAngle);
				AppendU64(static_cast<uint64>(Pair->State));
				AppendU64(static_cast<uint64>(Quantize2(Pair->AttackTimer)));
			}
			else
			{
				AppendU64(0);
				AppendU64(0);
				AppendU64(0);
				AppendU64(0);
				AppendU64(0);
				AppendU64(0);
			}
			if (const FChargeFragment* Charge = EntityView.GetFragmentDataPtr<FChargeFragment>())
			{
				AppendU64(static_cast<uint64>(Quantize2(Charge->DistanceTraveled)));
				AppendU64(static_cast<uint64>(Quantize2(Charge->ImpactSpeed)));
				AppendU64(Charge->bConnected);
			}
			else
			{
				AppendU64(0);
				AppendU64(0);
				AppendU64(0);
			}
			if (const FHealthFragment* Health = EntityView.GetFragmentDataPtr<FHealthFragment>())
			{
				AppendU64(static_cast<uint64>(static_cast<int16>(Health->MountCurHP)));
			}
			else
			{
				AppendU64(0);
			}
			// 8. Ranged terms (spec ranged-stack §3.4): appended AFTER the
			// charge block, same sorted-handle pass, per entity. Ammo count,
			// reload timer (1e-2 s), shots fired. Kind/RangeM/InitialAmmo
			// are spawn-immutable and excluded (the base-stats precedent).
			// Fixed term count with zero-fill when the fragment is absent
			// (melee compositions) keeps the stream position-stable.
			if (const FRangedStateFragment* Ranged = EntityView.GetFragmentDataPtr<FRangedStateFragment>())
			{
				AppendU64(static_cast<uint64>(static_cast<int16>(Ranged->AmmoCount)));
				AppendU64(static_cast<uint64>(Quantize2(Ranged->ReloadTimer)));
				AppendU64(static_cast<uint64>(Ranged->ShotsFired));
			}
			else
			{
				AppendU64(0);
				AppendU64(0);
				AppendU64(0);
			}
		}
	}

	// 9. Projectile pool block (spec ranged-stack §3.4, RC8): the live count,
	//    then per live entry in INDEX order (deterministic by construction --
	//    the pool's slot order is the free-list discipline, never a hash-map
	//    order). Position/velocity at 1e-2 m and m/s, TTL at 1e-2 s. The
	//    subsystem lands with the Phase 3 pipeline task; until it exists the
	//    block folds zero (position-stable, flags-off baseline untouched).
	const UBattleProjectileSubsystem* ProjectileSubsystem =
		World ? World->GetSubsystem<UBattleProjectileSubsystem>() : nullptr;
	const FBattleProjectilePool& Pool = ProjectileSubsystem
		? ProjectileSubsystem->GetPool()
		: FBattleProjectilePool::EmptyPool();
	AppendU64(static_cast<uint64>(Pool.GetLiveCount()));
	EndSection();
	for (int32 Slot = 0; Slot < Pool.GetCapacity(); ++Slot)
	{
		if (!Pool.IsLive(Slot))
		{
			continue;
		}
		AppendU64(static_cast<uint64>(Quantize2(Pool.PosX[Slot])));
		AppendU64(static_cast<uint64>(Quantize2(Pool.PosY[Slot])));
		AppendU64(static_cast<uint64>(Quantize2(Pool.PosZ[Slot])));
		AppendU64(static_cast<uint64>(Quantize2(Pool.VelX[Slot])));
		AppendU64(static_cast<uint64>(Quantize2(Pool.VelY[Slot])));
		AppendU64(static_cast<uint64>(Quantize2(Pool.VelZ[Slot])));
		AppendU64(static_cast<uint64>(Quantize2(Pool.TTL[Slot])));
	}

	EndSection();
	return CityHash64(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<uint32>(Bytes.Num()));
}
