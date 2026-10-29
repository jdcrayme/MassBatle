// BattleProjectilePool.h -- Phase 3 pooled projectile store (spec
// ranged-stack §3.3, ADR-27): a subsystem-owned structure-of-arrays, NOT Mass
// entities and NOT actors. 1,000 arrows must not cost archetype members or
// entity handles. Identity is (index, generation) -- the registry handle
// pattern miniaturized; a released slot's generation bumps so stale indices
// die. Capacity is reserved at battle start ONLY: Acquire beyond capacity
// fails closed into the Drops metric, never grows (RC9).
//
// Plain struct on the pure-math seam (B.0): meters, m/s, seconds. The
// subsystem wrapper (game thread) lands with the Phase 3 pipeline task; the
// pool itself is world-free and fully unit-testable.
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "BattleTypes.h"

struct MASSBATTLE_API FBattleProjectilePool
{
	static constexpr int32 InvalidIndex = -1;

	/** Reserve to Capacity; free-list seeded 0..Capacity-1; Drops = 0.
	 *  Re-reserving (battle restart) resets everything. Allocates ONLY here
	 *  -- never mid-volley (RC9, ground rule 3). */
	void Reserve(int32 Capacity);

	/** Take a slot; InvalidIndex + ++Drops when the free list is empty (the
	 *  shot is skipped by the caller -- fail closed, never an assert in
	 *  shipping paths). The acquired slot's generation is returned through
	 *  OutGeneration for identity checks. */
	int32 Acquire(uint32& OutGeneration);

	/** Return a slot; its generation bumps (stale indices die). */
	void Release(int32 Index);

	/** Live-slot test (a generation is current while acquired). */
	bool IsLive(int32 Index) const { return Index >= 0 && Index < Capacity && Live[Index] != 0; }
	bool IsValid(int32 Index, uint32 Generation) const
	{
		return IsLive(Index) && GenerationArray[Index] == Generation;
	}

	/** Battle/test reset: everything released, generations PRESERVED (stale
	 *  indices from a prior battle still die). */
	void Reset();

	int32 GetCapacity() const { return Capacity; }
	int32 GetLiveCount() const { return LiveCount; }
	int32 GetDrops() const { return Drops; }

	/** The shared empty pool (the state hash's null-subsystem fold, spec
	 *  §3.4): a zero-capacity static so the hash block reads a valid object
	 *  before the subsystem initializes. */
	static const FBattleProjectilePool& EmptyPool()
	{
		static FBattleProjectilePool Empty;
		return Empty;
	}

	// -- SoA arrays (all sized Capacity; live entries only are meaningful) --

	TArray<float> PosX;                 // meters
	TArray<float> PosY;
	TArray<float> PosZ;
	TArray<float> VelX;                 // m/s
	TArray<float> VelY;
	TArray<float> VelZ;
	TArray<float> GravityScale;         // profile multiplier
	TArray<float> TTL;                  // seconds; ground hit ends early
	TArray<uint8>  Kind;                // EBattleProjectileKind
	TArray<uint8>  Live;                // 0/1 slot state (iteration filter)
	TArray<uint32> GenerationArray;     // slot stamps (stale-index rejection)
	TArray<FUnitHandle> OwnerUnit;      // shooter's unit (attribution + friendly checks)
	TArray<FMassEntityHandle> ShooterEntity; // roll-count identity for impact resolution
	TArray<float> RangedAttack;         // stamped at fire (the impact attack term, spec §4.6)
	// Task 5 (spec §4.6): the transient roll context's shooter-side inputs,
	// stamped at fire so the impact pass never crosses chunks. ShooterRollCount
	// is the shooter's ShotsFired AT EMISSION -- the A13 run-invariant roll
	// seed; Lethality multiplies the impact hit chance (the melee pair path
	// reads it from the attacker's stats; a projectile has no chunks).
	TArray<uint32> ShooterRollCount;
	TArray<float> Lethality;

private:
	int32 Capacity = 0;
	int32 LiveCount = 0;
	int32 Drops = 0;
	TArray<int32> FreeIndices;          // LIFO stack
};
