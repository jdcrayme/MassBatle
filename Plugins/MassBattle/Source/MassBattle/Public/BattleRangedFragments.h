// BattleRangedFragments.h -- Phase 3 ranged data spine (spec ranged-stack
// §3.1, brief task 0). Blittable rules per arch §3.1: enums, fixed scalars
// -- no FString / UObject* / TArray. The fragment rides RANGED-CAPABLE
// compositions only (arch §3.2: "a spearman has no ranged fragments, so the
// ranged-fire processor never touches his chunks"); the archetype count grows
// by the bow-capable compositions (6 -> 8 expected, SpawnComposition
// re-signed -- spec §3.1). Unit conventions: fragment fields carry METERS
// where marked (seam convention) -- the fire pass is a seam consumer and
// converts once at the pool boundary, never per query.
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityElementTypes.h"
#include "Mass/EntityHandle.h"
#include "BattleTypes.h"
#include "BattleRangedFragments.generated.h"

/** Per-entity ranged state (spec ranged-stack §3.1; the architecture's
 *  FRangedStateFragment slot finally filled). Kind/RangeM/InitialAmmo are
 *  spawn-immutable (stamped from the EDU row at the spawn fixup); the rest
 *  is sim state and a state-hash term (spec §3.4). */
USTRUCT()
struct FRangedStateFragment : public FMassFragment
{
	GENERATED_BODY()

	// -- spawn-immutable (excluded from the state hash: base-stats
	//    precedent; mutation would be a spawn-path bug, not sim drift) --
	UPROPERTY() EBattleProjectileKind Kind = EBattleProjectileKind::None;
	/** Weapon range, meters (EDU stat_pri range value). */
	UPROPERTY() float RangeM = 0.f;
	/** Ammo per man at spawn (EDU stat_pri ammo value). */
	UPROPERTY() uint16 InitialAmmo = 0;

	// -- sim state (state-hash terms, spec §3.4) --
	/** Rounds remaining; decremented exactly once per emitted shot (RC5);
	 *  clamped at 0 (a decrement below zero is a loud bug, never silent). */
	UPROPERTY() int16 AmmoCount = 0;
	/** Seconds until the entity is eligible to fire again; handle-hash
	 *  staggered at spawn so volleys never serialize (RC1, ADR-05
	 *  discipline -- StaggeredPhaseOffset's family). */
	UPROPERTY() float ReloadTimer = 0.f;
	/** Lifetime shots fired (T3.3 accounting input + the deterministic
	 *  roll/dispersion seed's run-invariant count -- A13). */
	UPROPERTY() uint16 ShotsFired = 0;
};
