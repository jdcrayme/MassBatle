// BattleTypes.h -- plain data types of the battle data spine (arch §2).
// FUnitHandle is reflected (it travels inside fragments via UPROPERTY);
// FUnitRuntimeState / FUnitSpawnSpec are registry-side plain structs, never
// hot fragments (arch §3.1: no FString/TArray/UObject* in fragments -- these
// are not fragments).
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "BattleTypes.generated.h"

/** Unit identity: index into the registry + generation serial. Stale handles
 *  are detected by Serial mismatch (arch §2.3). Serial 0 is the null handle. */
USTRUCT()
struct FUnitHandle
{
	GENERATED_BODY()

	UPROPERTY() uint32 Index = 0;
	UPROPERTY() uint32 Serial = 0;

	bool IsValid() const { return Serial != 0; }
	bool operator==(const FUnitHandle& Other) const { return Index == Other.Index && Serial == Other.Serial; }
	bool operator!=(const FUnitHandle& Other) const { return !(*this == Other); }
};

inline uint32 GetTypeHash(const FUnitHandle& Handle)
{
	return HashCombine(::GetTypeHash(Handle.Index), ::GetTypeHash(Handle.Serial));
}

/** Plain-struct mirror of the closed GAS attribute set (arch §10.1). Cached in
 *  the registry; UnitSync derives the shared-fragment multipliers from it. */
struct FBattleAttributeSnapshot
{
	float Morale = 0.f;
	float Fatigue = 0.f;
	float Experience = 0.f;
	float ArmourUpgrade = 0.f;
	float MeleeAttackMod = 0.f;
	float MeleeDefenseMod = 0.f;
	float RangedAttackMod = 0.f;
	float SpeedMod = 0.f;
	float BlockChanceMod = 0.f;
	float OrderLatency = 0.f;
	float AmmoPool = 0.f;
};

/** Registry-side per-unit state (arch §2.3). Game-thread written, plain so
 *  Phase 1 workers can read through the access detector. */
struct FUnitRuntimeState
{
	/** Authoritative unit->entity bookkeeping (entity side is FOwnerUnitFragment). */
	TArray<FMassEntityHandle> Roster;

	/** Cached attribute values (updated by AUnitActor GAS delegates, arch §5.1). */
	FBattleAttributeSnapshot CachedAttributes;

	/** Per-slot generation counter; ++ on every (re)allocation so stale
	 *  FUnitHandles die. */
	int32 Generation = 0;

	// Phase 0: engagement-record stub intentionally empty (arch §2.3) --
	// engagement records arrive with the Phase 2 engagement solver.
};

/** Spawn request: one parsed EDU row + roster size. Formation is a stub until
 *  the Phase 1 slot layout exists. */
struct FUnitSpawnSpec
{
	/** Index of the EDU row this unit instantiates (Data/EDU fixture order). */
	int32 EduRowIndex = INDEX_NONE;

	/** Entities to spawn for this unit (fixture: 40). */
	int32 SoldierCount = 0;

	/** Debug-only display name; never referenced by sim logic. */
	FString UnitName;

	/** Formation stub (Phase 1 replaces with the layout-function preset). */
	float FormationSpacing = 1.2f;
};
