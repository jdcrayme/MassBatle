// BattleSpawnLibrary.h -- Route-A (archetype-first) batched spawn (arch §5.3,
// BRIEF §4.8): composition descriptor + ADR-14 shared fragment + BatchCreate +
// fixup pass. Pure data in / pure fragments out; no UObject coupling beyond the
// world subsystems. Task 6 (T0.3/T0.8).
#pragma once

#include "CoreMinimal.h"
#include "BattleEduParser.h"
#include "BattleTypes.h"
#include "BattleSpawnLibrary.generated.h"

/** Typed stats mapped off one parsed EDU row (arch §10.2 mirror of the fixture
 *  schema; parser stays generic, this is the game-side view). */
struct FBattleRowStats
{
	FString TypeName;
	int32 SoldierCount = 0;
	float BaseHealth = 1.f;
	float MeleeAttack = 0.f;
	float MeleeDefense = 0.f;
	float RangedAttack = 0.f;   // fixture has no stat_sec; 0 until ranged rows exist
	float Armor = 0.f;
	float MoraleBase = 0.f;
	float ChargeDistance = 0.f;

	// Phase 2 combat mapping (task 2 step 1, R-C10): the fixture ALREADY
	// carries the structure -- stat_pri weapon_type ("blade_piercing") is the
	// AP flag, stat_pri tech type ("spear") is the weapon class, the first
	// decimal field is the lethality multiplier, stat_health's second value is
	// the mount HP, and stat_charge_dist was already mapped. These are typed
	// extractions of EXISTING parsed fields: no parser/schema change, the T0.2
	// golden round-trip is untouched (no re-sign). Exact field identities and
	// constants are A6 placeholders for the Phase-6 T6.1 audit.
	uint8 ReachClass = static_cast<uint8>(EBattleWeaponReach::Normal); // >= Spear = second rank
	bool bArmorPiercing = false;
	float Lethality = 1.f;      // multiplies the hit chance (spec §4.8)
	int32 MountHP = 0;          // stat_health mount pool (0 for foot)

	// capability tags (arch §3.2) derived from category/class
	bool bInfantry = false;
	bool bCavalry = false;
	bool bMelee = false;
	bool bBow = false;

	/** Map one parsed FEduUnit row onto typed stats. Fails loudly on missing fields. */
	static MASSBATTLE_API bool FromEdu(const FEduUnit& Row, FBattleRowStats& OutStats, FString& OutError);
};

struct FBattleSpawnResult
{
	FUnitHandle Unit;
	int32 NumEntities = 0;
	double ElapsedUs = 0.0;
};

UCLASS()
class MASSBATTLE_API UBattleSpawnLibrary : public UObject
{
	GENERATED_BODY()

public:
	/** Batched Route-A spawn of one unit's roster (game thread, NOT while Mass
	 *  is processing -- guarded by checkf). Registers the unit, builds the
	 *  per-role archetype, creates Count entities with the ADR-14 anchored
	 *  shared fragment, runs the fixup pass, publishes the roster. */
	static FBattleSpawnResult SpawnUnit(UWorld* World, const FUnitSpawnSpec& Spec, const FBattleRowStats& Row);

	/** Convenience: parse + map + spawn. */
	static bool SpawnUnitFromEduRow(UWorld* World, const FUnitSpawnSpec& Spec, const FEduUnit& Row,
		FBattleSpawnResult& OutResult, FString& OutError);
};
