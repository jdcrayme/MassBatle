// BattleEngagement.h -- engagement solver pure math on the B.0 seam (brief A1;
// phase ground rule 1). ALL functions are free functions / plain structs,
// deterministic, no RNG, no wall clock, METERS. Processors stay thin shims:
// read fragments/records -> call these -> write fragments. RNG discipline
// (A13/F7): the assign-pass shuffle draws from the registered `Combat.Assign`
// stream in the CALLER (processor), never in here.
//
// Task 0 skeleton: the spec §4 signatures exist and return failure sentinels;
// bodies land in task 4 (brief task plan).
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "BattleTypes.h"

/** Battle.Combat.* CVAR block (spec §4.11 table; brief task 0 step 4; B.8
 *  item 6). Defined in BattleEngagement.cpp; read through these accessors
 *  from any TU. Defaults are placeholders pending the Phase-6 T6.1 audit
 *  (A6). `Battle.Combat.Enable` flips to default 1 only when task 7's
 *  resolution pass lands end-to-end (ground rule 8). */
namespace FCombatCVars
{
	// Master flag: 0 = pre-combat behavior (flags-off path byte-identical to
	// the pre-combat baseline; the unchanged T1.8 soak proves it).
	MASSBATTLE_API int32 Enable();
	// Engagement-record recompute period, seconds (4 Hz default).
	MASSBATTLE_API float ClusterPeriod();
	// Per-defender budget ceiling (mob mode).
	MASSBATTLE_API int32 MaxMobSlots();
	// Per-defender budget floor (formed line).
	MASSBATTLE_API int32 FormedSlots();
	// Spear-reach budget add (second rank strikes through the front rank).
	MASSBATTLE_API int32 SecondRankBonus();
	// Same-unit living neighbors that read as full coverage.
	MASSBATTLE_API int32 NeighborsForFullCoverage();
	// Exposed-angle quantization buckets around a defender.
	MASSBATTLE_API int32 SectorCount();
	// Mob DesiredPressure multiplier (defenderAlive * factor, floored at
	// MaxMobSlots -- the A14 pressure floor).
	MASSBATTLE_API int32 MobPressureFactor();
	// Distance-breakup age gate, seconds (0.3-0.5 s band; R-C1 churn guard).
	MASSBATTLE_API float PairHysteresis();
	// Base attack-cycle interval, seconds (staggered per entity).
	MASSBATTLE_API float AttackInterval();
	// Contested-roll clamps.
	MASSBATTLE_API float HitChanceFloor();
	MASSBATTLE_API float HitChanceCap();
	// Fresh-contact charge gate, m/s.
	MASSBATTLE_API float ChargeSpeedThreshold();
	// Charge-bonus decay, seconds.
	MASSBATTLE_API float ChargeDecayWindow();
	// Soak clash-mix variant (nightly; default 0 = T1.8 baseline untouched).
	MASSBATTLE_API int32 SoakClash();
	// I4 StateTree prototype flag (default 0; task 1 timebox).
	MASSBATTLE_API int32 StateTree();
}

/** Read-only defender snapshot for the seam (plain struct, meters). Grows
 *  with task 4's real implementation needs (slot-table access rides the
 *  caller's records, not this view). */
struct FCombatDefenderView
{
	/** Defender entity position, meters. */
	FVector2D Pos = FVector2D::ZeroVector;
	/** Defender's file spacing, meters (coverage search radius input). */
	float Spacing = 0.f;
	/** Defender's unit (same-unit neighbors define coverage). */
	FUnitHandle Unit;
	/** The defender entity itself. */
	FMassEntityHandle Entity;
};

/** Read-only attacker snapshot for the seam (plain struct). */
struct FCombatAttackerView
{
	/** Attacker's unit (engagement-record lookup key). */
	FUnitHandle Unit;
	/** Weapon reach class (>= Spear grants the second-rank budget bonus). */
	uint8 ReachClass = 0;
};

namespace FCombatSolver
{
	// -- ContactAngle conventions (ONE value, TWO consumers, spec §4.5) --
	//
	// ContactAngle is stored 0-255 (full circle around the defender). Angle 0
	// is the defender's facing direction; the angle grows CLOCKWISE (viewed
	// from above, standard UE +Z-down planar convention) from facing toward
	// the attacker's approach direction (attacker - defender). The defense
	// arc bands (BattleMelee) and the contact-ring steering (servo, task 6)
	// both read this ONE quantized value; nothing re-derives the angle.

	/** Quantizes a 0-255 angle into [0, SectorCount) buckets (floor division:
	 *  the bucket owns [k*256/SectorCount, (k+1)*256/SectorCount); boundaries
	 *  pinned by the T2.1 golden). */
	MASSBATTLE_API int32 SectorIndexFromAngle(uint8 ContactAngle, int32 SectorCount);

	/** Center of a sector as a 0-255 angle (round-trips through
	 *  SectorIndexFromAngle for odd/even sector counts). */
	MASSBATTLE_API uint8 SectorCenterAngle(int32 SectorIndex, int32 SectorCount);

	/** Per-defender attack-slot budget (spec §4.5, arch §7.3 verbatim):
	 *  lerp(MaxMobSlots, FormedSlots, Coverage), + SecondRankBonus for
	 *  spear-class attackers, clamped >= 1. FAILURE STUB: returns 0. */
	MASSBATTLE_API int32 SlotBudget(const FCombatDefenderView& Defender, float Coverage, const FCombatAttackerView& Attacker);

	/** Same-unit living-neighbor coverage in [0,1] (spec §4.5): neighbors
	 *  within Spacing * 1.25, normalized by NeighborsForFullCoverage, capped
	 *  at 1. FAILURE STUB: returns -1.f. */
	MASSBATTLE_API float ComputeCoverage(const FCombatDefenderView& Defender);

	/** Quantized exposed approach sector around the defender (spec §4.5):
	 *  exposed sectors filled before covered ones; 0-255 quantized (full
	 *  circle). FAILURE STUB: returns 255. */
	MASSBATTLE_API uint8 PickExposedAngle(const FCombatDefenderView& Defender, float Coverage);

	/** Unit-level engagement pressure (spec §3.2, with the A14 floor):
	 *  frontal = DefenderAlive * FormedSlots;
	 *  mob = Min(AttackerAlive, Max(DefenderAlive * MobPressureFactor, MaxMobSlots)).
	 *  Mode: 0 = Frontal, 1 = Flank (frontal formula; flanking attacker
	 *  files wrap the line end), 2 = Mob. FAILURE STUB: returns 0. */
	MASSBATTLE_API int32 DesiredPressure(int32 Mode, int32 AttackerAlive, int32 DefenderAlive);
}
