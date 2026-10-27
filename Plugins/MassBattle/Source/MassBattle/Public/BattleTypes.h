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
struct MASSBATTLE_API FBattleAttributeSnapshot
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

/** Thin order API payload (decision A2): Move to an anchor with a facing, Stop,
 *  or Garrison into / ExitGarrison from a structure. Positions in centimeters
 *  (fragment boundary convention), speeds in m/s (project math convention) --
 *  converted by the consumers. Selection / picking / UI / camera stay out of
 *  scope v1 (Phase 5 territory). */
struct FUnitOrder
{
	// Phase 2 (brief A4): AttackUnit orders a specific enemy unit; AttackMove
	// marches and auto-engages en route. Disengage = re-task with Move/Stop
	// while engaged (records die -> mass release, spec §4.7).
	enum class EType : uint8 { None, Move, Stop, Garrison, ExitGarrison, AttackMove, AttackUnit };

	EType Type = EType::None;
	/** Move-order anchor (centimeters, engine space). */
	FVector2D AnchorPos = FVector2D::ZeroVector;
	/** Move-order final facing (degrees). */
	float FacingYaw = 0.f;
	/** Commanded march speed (m/s); 0 -> the default walk speed. */
	float CommandedSpeed = 0.f;
	/** Commanded frontage (entities per rank). 0 = keep the current layout;
	 *  >0 re-lays slots on the next plan (clamped to NumSlots by UnitLocomotion). */
	int32 CommandedFiles = 0;
	/** Garrison-order target structure (registry-assigned id; 0 = none). */
	uint32 StructureId = 0;
	/** Attack-order target unit (brief A4; valid for AttackUnit, the
	 *  pre-designated target for AttackMove when issued with one). */
	FUnitHandle TargetUnit;
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

	/** Current order (A2 thin command API; UnitLocomotion consumes). */
	FUnitOrder CurrentOrder;
	/** Bumped on every issued order -- UnitLocomotion's change detector. */
	uint32 OrderSerial = 0;

	/** Order parked while the unit egresses (A7): a Move (or re-Garrison)
	 *  received while garrisoned resumes at the assembly point. */
	FUnitOrder PendingAfterEgress;

	// Phase 0: engagement-record stub intentionally empty (arch §2.3) --
	// engagement records arrive with the Phase 2 engagement solver.
};

/** Spawn request: one parsed EDU row + roster size. Formation params seed the
 *  Phase 1 movement spine (FUnitFormationSharedFragment). SpawnPos is engine
 *  units (centimeters); FormationSpacing is meters (project convention) --
 *  converted at the fragment boundary (phase-1 ground rule 2). */
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

	/** Formation anchor at spawn, planar (centimeters, engine space). */
	FVector2D SpawnPos = FVector2D::ZeroVector;

	/** Commanded facing at spawn (degrees). */
	float SpawnYaw = 0.f;

	/** Ranks in the formed layout (files = ceil(SoldierCount / NumRanks)). */
	int32 NumRanks = 2;
};
