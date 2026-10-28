// BattleSave.h -- save/load skeleton schema v1 (brief A8, R7; spec §3.5).
// Plain structs only (Iris-ready, B.8 item 7): no UObject pointers, no raw
// pointers across the registry boundary. Entities are NOT serialized -- the
// restore path rebuilds from spec through the spawn library (ADR-17 Route A)
// and overwrites fragment payloads (T6.3 maturity); Phase 2 asserts the
// registry-level round-trip (capture -> clear -> restore -> hash equal).
#pragma once

#include "CoreMinimal.h"
#include "BattleTypes.h"

#include "BattleSave.generated.h"

/** One unit's registry runtime snapshot. Spec + roster handles ride along:
 *  T6.3's rebuild-from-spec consumes Spec; v1's registry round-trip restores
 *  the bookkeeping verbatim (no entities exist in the v1 assertion). */
USTRUCT()
struct FUnitSaveEntry
{
	GENERATED_BODY()

	UPROPERTY() uint32 Index = 0;   // slot index
	UPROPERTY() uint32 Serial = 0;  // live handle serial (== Generation)
	UPROPERTY() int32 Generation = 0;
	UPROPERTY() FString UnitName;   // from Spec (plain, registry-side)
	UPROPERTY() int32 EduRowIndex = INDEX_NONE;
	UPROPERTY() int32 SoldierCount = 0;
	UPROPERTY() float FormationSpacing = 1.2f;
	UPROPERTY() FVector2D SpawnPos = FVector2D::ZeroVector;
	UPROPERTY() float SpawnYaw = 0.f;
	UPROPERTY() int32 NumRanks = 2;

	/** Entity handles (v1: verbatim; T6.3 replaces with rebuilt entities). */
	UPROPERTY() TArray<FMassEntityHandle> Roster;

	UPROPERTY() float Morale = 0.f;
	UPROPERTY() float Fatigue = 0.f;
	UPROPERTY() float Experience = 0.f;
	UPROPERTY() float ArmourUpgrade = 0.f;
	UPROPERTY() float MeleeAttackMod = 0.f;
	UPROPERTY() float MeleeDefenseMod = 0.f;
	UPROPERTY() float RangedAttackMod = 0.f;
	UPROPERTY() float SpeedMod = 0.f;
	UPROPERTY() float BlockChanceMod = 0.f;
	UPROPERTY() float OrderLatencyAttr = 0.f;
	UPROPERTY() float AmmoPool = 0.f;

	// Current + parked orders (payload fields verbatim).
	UPROPERTY() uint8 OrderType = 0;
	UPROPERTY() FVector2D OrderAnchorPos = FVector2D::ZeroVector;
	UPROPERTY() float OrderFacingYaw = 0.f;
	UPROPERTY() float OrderCommandedSpeed = 0.f;
	UPROPERTY() int32 OrderCommandedFiles = 0;
	UPROPERTY() uint32 OrderStructureId = 0;
	UPROPERTY() uint32 OrderTargetUnitIndex = 0;
	UPROPERTY() uint32 OrderTargetUnitSerial = 0;
	UPROPERTY() uint32 OrderSerial = 0;

	UPROPERTY() uint8 PendingOrderType = 0;
	UPROPERTY() FVector2D PendingAnchorPos = FVector2D::ZeroVector;
	UPROPERTY() float PendingFacingYaw = 0.f;
	UPROPERTY() float PendingCommandedSpeed = 0.f;
	UPROPERTY() int32 PendingCommandedFiles = 0;
	UPROPERTY() uint32 PendingStructureId = 0;
	UPROPERTY() uint32 PendingTargetUnitIndex = 0;
	UPROPERTY() uint32 PendingTargetUnitSerial = 0;

	// Phase 3 (spec ranged-stack §3.5, v2): fire-control toggles + the
	// ammo-fraction denominator. Entity ammo and pool state stay unsaved
	// (T6.3 by design): the v2 restore reproduces registry-level ranged
	// state only. v1 payloads restore these to the defaults (toggles
	// on/off/off, denominators 0).
	UPROPERTY() uint8 bFireAtWill = 1;
	UPROPERTY() uint8 bSkirmish = 0;
	UPROPERTY() uint8 bOutOfAmmo = 0;
	UPROPERTY() int32 RangedInitialAmmo = 0;
	UPROPERTY() int32 InitialRosterSize = 0;
};

/** One engagement-record snapshot (spec §3.5). AssignedAttackers is excluded:
 *  it is tallied fresh each tick and never persisted (spec §3.2). */
USTRUCT()
struct FEngagementSaveEntry
{
	GENERATED_BODY()

	UPROPERTY() uint32 AttackerIndex = 0;
	UPROPERTY() uint32 AttackerSerial = 0;
	UPROPERTY() uint32 DefenderIndex = 0;
	UPROPERTY() uint32 DefenderSerial = 0;
	UPROPERTY() uint8 Mode = 0;
	UPROPERTY() float FrontageStart = 0.f;
	UPROPERTY() float FrontageEnd = 0.f;
	UPROPERTY() int32 DesiredPressure = 0;
	UPROPERTY() uint8 bDead = 0;
};

/** One RNG stream state (name + current position seed; FRandomStream mutates
 *  Seed in place, so the current seed IS the stream state). */
USTRUCT()
struct FRngStreamSaveEntry
{
	GENERATED_BODY()

	UPROPERTY() FName StreamName;
	UPROPERTY() int32 CurrentSeed = 0;
};

/** Schema-versioned save container (v1: registry runtime snapshot + RNG
 *  stream states; v2 adds the ranged fire-control toggles + fraction
 *  denominators per spec ranged-stack §3.5; shared-fragment payloads join
 *  the schema as entities do at T6.3). Plain data only. */
USTRUCT()
struct FBattleSaveData
{
	GENERATED_BODY()

	UPROPERTY() uint32 SchemaVersion = 2;

	UPROPERTY() TArray<FUnitSaveEntry> Units;
	/** Free-slot reuse state (LIFO order preserved) + per-slot generation
	 *  counters + the next-never-used index -- the bookkeeping hash folds
	 *  free-list depth, so the round-trip must restore them exactly. */
	UPROPERTY() TArray<uint32> FreeIndices;
	UPROPERTY() TArray<uint32> FreeSlotGenerationCounters;
	UPROPERTY() uint32 NextIndexValue = 1;

	UPROPERTY() TArray<FEngagementSaveEntry> Engagements;

	UPROPERTY() int32 RngRootSeed = 0;
	UPROPERTY() TArray<FRngStreamSaveEntry> RngStreams;
};
