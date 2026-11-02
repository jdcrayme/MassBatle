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
#include "MassMovementFragments.h"
#include "Engine/World.h"

namespace
{
	// Presence probe for the census: tags answer HasTag, fragments
	// GetFragmentDataPtr -- one idiom for the mixed kCensusTypes list (the
	// two idioms the hash already uses).
	template<typename T>
	bool CensusHasType(FMassEntityView& View)
	{
		if constexpr (TIsDerivedFrom<T, FMassTag>::IsDerived)
		{
			return View.HasTag<T>();
		}
		else
		{
			return View.GetFragmentDataPtr<T>() != nullptr;
		}
	}

	// The census bitset (task 1.S3-c, owner decision H-1 option A): one bit
	// per type, the ARGUMENT ORDER below is kCensusTypes[] -- this template
	// argument list is the ONE HOME of the census type list (AGENTS §7); a
	// type's bit index is its argument position. Maintenance rule (the phase
	// checklist): any new fragment/tag that can join a battle composition
	// extends this list (the §5 guard test watches the death path).
	// Fixed width (one U64 per entity) keeps the byte stream position-stable
	// when a composition is absent (the zero-fill precedent).
	template<typename... Ts>
	uint64 CensusBits(FMassEntityView& View)
	{
		static_assert(sizeof...(Ts) <= 64, "census bitset overflows 64 bits -- widen the fold");
		uint64 Bits = 0;
		int32 Bit = 0;
		auto Fold = [&Bits, &Bit](const bool bPresent)
		{
			if (bPresent)
			{
				Bits |= (1ull << Bit);
			}
			++Bit;
		};
		(Fold(CensusHasType<Ts>(View)), ...);
		return Bits;
	}
}

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
	// Section machinery (task 1.S3-b, owner decision H-2): each section
	// hashes its OWN byte slice into OutSectionHashes. The old EndSection
	// hashed the cumulative PREFIX -- one diverging section forced every
	// later section hash to differ (the bisector cascade; measured
	// 2026-11-02, det_census.log sample 0: sections 4-6 DIFF purely as the
	// prefix of the section-3 count term, zero independent signal). The
	// MAIN byte stream is unchanged -- every term still appends to Bytes in
	// the same fixed order -- but sections 4-8 interleave per unit inside
	// the sorted walk, so each section accumulates its own slice and the
	// hashes are per-section content only. Indices stay stable at 1..10
	// (the bisector's names keep their meaning; 4-8 gain hashes they never
	// had). No goldens are stored anywhere -- the bisector only prints.
	TArray<uint8> SectionBytes[10];
	int32 CurrentSection = 0;
	auto Append = [&Bytes, &SectionBytes, &CurrentSection](const void* Data, int32 Size)
	{
		Bytes.Append(static_cast<const uint8*>(Data), Size);
		if (CurrentSection >= 1 && CurrentSection <= 10)
		{
			SectionBytes[CurrentSection - 1].Append(static_cast<const uint8*>(Data), Size);
		}
	};
	auto AppendU64 = [&Append](uint64 V)
	{
		Append(&V, sizeof(V));
	};
	auto SetSection = [&CurrentSection](const int32 Section)
	{
		CurrentSection = Section;
	};
	auto EndSection = [OutSectionHashes, &SectionBytes](const int32 Section)
	{
		if (OutSectionHashes)
		{
			const TArray<uint8>& Slice = SectionBytes[Section - 1];
			OutSectionHashes->Add(CityHash64(reinterpret_cast<const char*>(Slice.GetData()),
				static_cast<uint32>(Slice.Num())));
		}
	};

	// 1. Registry bookkeeping (sorted handles -> serials -> roster sizes;
	// dirty flags excluded -- transient pipeline state, audit M4).
	SetSection(1);
	uint64 Bookkeeping = Registry->ComputeBookkeepingHash();
	AppendU64(Bookkeeping);
	EndSection(1);

	// 2. Per-unit entity state, in roster order: HP (quantized 1e-2), dead flag,
	// transform (quantized 1 cm; yaw at 0.1 deg -- phase-1 ground rule 7).
	SetSection(2);

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

	EndSection(2);
	// 3. The live composition census (task 1.S3-c, owner decision H-1 option
	// A) -- the retired term here was DebugGetArchetypesCount(), the B.4
	// unbounded-growth watch. That count is a SESSION-CUMULATIVE, process-
	// history counter (archetypes are created on demand and never destroyed
	// within the process -- the repo's own warm-up comment proved it,
	// BattleFuncTestActor SpawnMixedFixture), so under PERFECT determinism
	// run 2's count carries run 1's whole history and the term diverges at
	// every sample between run 1's first and last first-time composition
	// creation -- measured 2026-11-02 (det_census.log): run 1 sampled
	// 4->6->8 (the death-variant milestones), run 2 constant 8 at every
	// sample, sections 1-2 matching at the first diverging sample. The count
	// also violated this file's own header contract (no non-sim state). The
	// replacement folds per entity, over the SAME canonical sorted-roster
	// walk, a fixed-width presence bitset over kCensusTypes (the census
	// template list above) -- symmetric across runs by construction, live
	// state only, Shipping-safe (no WITH_MASSENTITY_DEBUG dependency). The
	// cumulative count stays a METRIC: Battle.DumpMetrics + the gate's
	// Det2RunCensus telemetry line.
	SetSection(3);
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		if (!Roster)
		{
			continue;
		}
		for (const FMassEntityHandle& Entity : *Roster)
		{
			FMassEntityView View(EntityManager, Entity);
			AppendU64(CensusBits<
				// Route A spawn composition (BattleSpawnLibrary order).
				FOwnerUnitFragment, FLoadoutFragment, FBaseStatsFragment,
				FHealthFragment, FTransformFragment, FMassVelocityFragment,
				FSlotAssignmentFragment, FSlotTargetFragment,
				FMovementStateFragment, FServoStateFragment,
				FCombatIntentFragment, FCombatPairFragment, FChargeFragment,
				FRangedStateFragment,
				// Trait tags.
				FInfantryTag, FCavalryTag, FMeleeTag, FBowTag,
				// The death path's only runtime composition mutators (F4).
				FDeadTag, FDeadSignaledTag>(View));
		}
	}
	EndSection(3);

	// 4. Per-unit shared stat values (quantized), in the same sorted order.
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		if (!Roster || Roster->Num() == 0)
		{
			continue;
		}
		const FMassEntityView View(EntityManager, (*Roster)[0]);
		SetSection(4);
		if (const FUnitStatsSharedFragment* Stats = EntityManager.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*Roster)[0]))
		{
			AppendU64(static_cast<uint64>(Quantize2(Stats->SpeedMult)));
			AppendU64(static_cast<uint64>(Quantize2(Stats->MeleeAttackMult)));
			AppendU64(static_cast<uint64>(Quantize2(Stats->MeleeDefenseMult)));
			AppendU64(static_cast<uint64>(Quantize2(Stats->MoraleBonus)));
		}
		// 5. Formation movement state (phase-1 ground rule 7): anchor at 1 cm,
		// facing at 0.1 deg, speed limit + spacing scale at 1e-2.
		SetSection(5);
		if (const FUnitFormationSharedFragment* Formation = EntityManager.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]))
		{
			AppendU64(static_cast<uint64>(FMath::RoundToDouble(static_cast<double>(Formation->AnchorPos.X))));
			AppendU64(static_cast<uint64>(FMath::RoundToDouble(static_cast<double>(Formation->AnchorPos.Y))));
			AppendU64(static_cast<uint64>(FMath::RoundToDouble(static_cast<double>(Formation->FacingYaw) * 10.0)));
			AppendU64(static_cast<uint64>(Quantize2(Formation->SpeedLimit)));
			AppendU64(static_cast<uint64>(Quantize2(Formation->SpacingScale)));
			AppendU64(static_cast<uint64>(Formation->Generation));
			SetSection(6);
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
			SetSection(7);
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
			SetSection(8);
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
	// Sections 4-8 were interleaved per unit above (stats/formation/garrison
	// once per unit, then combat + ranged per entity); their buffers are
	// complete only after the walk -- finalize them in index order here.
	EndSection(4);
	EndSection(5);
	EndSection(6);
	EndSection(7);
	EndSection(8);

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
	SetSection(9);
	AppendU64(static_cast<uint64>(Pool.GetLiveCount()));
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

	// 10. Unit-level morale block (spec morale-and-gas section 3.4): appended
	//     AFTER the pool block (existing section indices stay stable), per
	//     sorted handle, fixed term count. RoutState/RoutCount as uint8,
	//     ExertionIntegral/LastMoraleValue/LastFatigueValue/FreshCasualtySpike
	//     at 1e-2, bGuardMode as uint8. The tick stamp is pipeline state
	//     (excluded, the dirty-flag precedent); discipline/officer identity
	//     are spawn-immutable (excluded, the base-stats precedent).
	EndSection(9);
	SetSection(10);
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		const FUnitRuntimeState* State = Registry->FindUnitState(Handle);
		if (!State)
		{
			continue;
		}
		AppendU64(static_cast<uint64>(State->RoutState));
		AppendU64(static_cast<uint64>(State->RoutCount));
		AppendU64(static_cast<uint64>(Quantize2(State->ExertionIntegral)));
		AppendU64(static_cast<uint64>(Quantize2(State->LastMoraleValue)));
		AppendU64(static_cast<uint64>(Quantize2(State->LastFatigueValue)));
		AppendU64(static_cast<uint64>(Quantize2(State->FreshCasualtySpike)));
		AppendU64(State->bGuardMode ? 1u : 0u);
	}

	EndSection(10);
	return CityHash64(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<uint32>(Bytes.Num()));
}
