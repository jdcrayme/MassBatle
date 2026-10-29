// BattleEngagement.h -- engagement solver pure math on the B.0 seam (brief A1;
// phase ground rule 1). ALL functions are free functions / plain structs,
// deterministic, no wall clock, METERS. Processors stay thin shims: read
// fragments/records -> call these -> write fragments. RNG discipline (A13/F7):
// the only randomness is the assign-pass seeker shuffle, driven by a
// caller-provided `Combat.Assign` FRandomStream (null = the B.5 determinism
// mode: index-sorted iteration with per-entity jitter).
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "BattleTypes.h"

struct FCombatTallyTable;
struct FRandomStream;

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

/** Read-only defender snapshot for the seam (plain struct, meters). */
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
	// -- Body geometry (ONE home for the 40 cm soldier body radius, fact 1.9) --
	//
	// Consumers: the cluster pass's OBB dilation, the assign pass's contact
	// ring, and the servo's avoidance/touch radii all re-derive the same
	// body size. Keep it HERE (beside FAssignContext, which carries it into
	// the seam) -- a per-file retyped 0.4f drifts silently (B-1).
	namespace Contact
	{
		/** Soldier body radius, meters (fact 1.9: FAgentRadiusFragment 40 cm). */
		constexpr float BodyRadiusM = 0.4f;
	}

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
	 *  SectorIndexFromAngle). */
	MASSBATTLE_API uint8 SectorCenterAngle(int32 SectorIndex, int32 SectorCount);

	/** Per-defender attack-slot budget (spec §4.5, arch §7.3 verbatim):
	 *  lerp(MaxMobSlots, FormedSlots, Coverage) rounded to nearest, +
	 *  SecondRankBonus for spear-class attackers, clamped >= 1. T2.2 bound
	 *  reading: [1, MaxMobSlots] non-spear; MaxMobSlots + SecondRankBonus for
	 *  spear (the bonus is geometry, not a bound violation). */
	MASSBATTLE_API int32 SlotBudget(const FCombatDefenderView& Defender, float Coverage, const FCombatAttackerView& Attacker);

	/** Same-unit living-neighbor coverage in [0,1] (spec §4.5): neighbors
	 *  within Spacing * 1.25 (the caller passes same-UNIT, living, self-
	 *  excluded positions), normalized by NeighborsForFullCoverage, capped
	 *  at 1. */
	MASSBATTLE_API float ComputeCoverage(const FCombatDefenderView& Defender, const TArray<FVector2D>& SameUnitNeighborPositionsM);

	/** Sector-assignment input: which sectors are covered by living same-unit
	 *  neighbors, and how many attackers this pass already placed per sector. */
	struct FSectorPickContext
	{
		/** Bit k set = sector k holds a living same-unit neighbor (covered). */
		uint16 CoveredMask = 0;
		/** Assigned counts per sector this pass (SectorCount <= 16). */
		int32 AssignedPerSector[16] = {};
		/** The defender's current total budget (bounds the sector shares). */
		int32 TotalBudget = 0;
	};

	/** Quantized approach angle for the NEXT attacker (spec §4.5): prefer the
	 *  least-loaded EXPOSED sector (ties -> lowest index) while exposed
	 *  sectors are below their even share of the budget; only then open
	 *  covered arcs (same rule). Returns 255 when every sector is at share
	 *  (the caller treats it as "no angle available this pass"). */
	MASSBATTLE_API uint8 PickExposedAngle(int32 SectorCount, const FSectorPickContext& Context);

	/** Unit-level engagement pressure (spec §3.2, with the A14 floor):
	 *  frontal/flank = DefenderAlive * FormedSlots;
	 *  mob = Min(AttackerAlive, Max(DefenderAlive * MobPressureFactor, MaxMobSlots)). */
	MASSBATTLE_API int32 DesiredPressure(int32 Mode, int32 AttackerAlive, int32 DefenderAlive);

	// -- The assign step (spec §4.4 pseudo-code as the contract; task 6's
	//    AssignPairsProcessor bridges fragments/records into these mirrors). --

	/** Mirror of one seeker's mutable assign state (FCombatPairFragment +
	 *  the order intent the processor resolves). */
	struct FAssignSeekerState
	{
		FMassEntityHandle Entity;
		FUnitHandle OwnerUnit;
		int32 RosterIndex = -1;   // canonical identity input (A13, RC8)
		FUnitHandle IntentTarget;      // enemy UNIT this seeker's owner engages
		FVector2D PosM = FVector2D::ZeroVector;
		float PairAge = 0.f;
		float AttackTimer = 0.f;
		FMassEntityHandle Target;      // paired defender; invalid = unpaired
		uint8 ContactAngle = 0;
		uint8 State = 0;               // ECombatPairState
		uint8 ReachClass = 0;
		uint8 LaneIndex = 0;           // attacking file (laned targeting)
		uint8 BackoffTicks = 0;        // failed-seek backoff (1-3, jittered)
		uint8 bCharging = 0;           // charging entities bypass pairing (§4.7)
		int32 RecordIndex = INDEX_NONE; // into Records; INDEX_NONE = no engagement
	};

	/** Mirror of one candidate defender. */
	struct FAssignDefenderState
	{
		FMassEntityHandle Entity;
		FUnitHandle Unit;
		FVector2D PosM = FVector2D::ZeroVector;
		float Spacing = 0.f;
		float YawDeg = 0.f;            // defender facing (ring-point math)
		float Defense = 0.f;           // immutable stat mirror (resolution input)
		float Shield = 0.f;
		float Armor = 0.f;
		int32 MountHP = 0;
		float Coverage = 0.f;          // stamped fresh by the tally pass
		uint16 CoveredMask = 0;        // same-unit living-neighbor sectors
		uint8 LaneIndex = 0;
		uint8 bValidTarget = 1;        // alive + not garrisoned + not own unit
	};

	/** Mirror of one engagement record (AssignedAttackers is the mutable
	 *  tally the pass owns). */
	struct FAssignRecordView
	{
		int32 DesiredPressure = 0;
		int32 AssignedAttackers = 0;
		uint8 Mode = 0;                // EEngagementMode
		uint8 bDead = 0;               // disengage flag -> mass release (§4.7)
	};

	struct FAssignContext
	{
		/** `Combat.Assign` stream (A13); null = B.5 determinism mode
		 *  (index-sorted iteration + per-entity jitter). */
		FRandomStream* ShuffleStream = nullptr;
		/** Per-defender per-sector assignment counts (caller-owned scratch,
		 *  Defenders.Num() x 16; the pass Reserves once, Resets per pass --
		 *  EC9). Null degrades sector picks to covered-mask-only. */
		TArray<int32>* SectorScratch = nullptr;
		float SeekRadiusM = 15.f;      // arch §12: 12-15 m
		float BreakRadiusFactor = 1.5f; // ring x 1.5 (arch §12)
		float DefenderRadiusM = 0.4f;  // fact 1.9 body radius
		float AttackerRadiusM = 0.4f;
		/** Weapon reach in meters by reach class (index =
		 *  EBattleWeaponReach); the contact ring + break radius inputs. */
		float ReachByClassM[8] = { 0.5f, 0.7f, 1.6f, 2.2f, 2.8f, 0.7f, 0.7f, 0.7f };
	};

	/** Ring distance for a seeker's reach class about a defender:
	 *  DefenderRadius + AttackerRadius + reach (the seam's ONE ring
	 *  formula -- the write-back's ring points and the pass's break
	 *  radius must not re-derive it, B-1). Reach clamped to the table. */
	MASSBATTLE_API float RingRadiusFor(uint8 ReachClass, const FAssignContext& Context);

	/** Binary search over the handle-sorted defender table (the seam
	 *  sorts it; callers MUST sort handle-wise before searching). Exposed
	 *  so processors can stop linear-scanning the mirror (A-4). */
	MASSBATTLE_API const FAssignDefenderState* FindDefender(const TArray<FAssignDefenderState>& Defenders, FMassEntityHandle Entity);

	// -- Cluster pass pure math (task 5; spec §4.2). The processors are thin
	//    shims: gather unit bodies + contact pairs, call these, write records. --

	/** Unit-level contact view for the cluster pass. */
	struct FUnitClusterNode
	{
		FUnitHandle Unit;
		FVector2D AnchorM = FVector2D::ZeroVector;
		float YawDeg = 0.f;
		float HalfAlongM = 0.f;   // frontage half-extent (files spread)
		float HalfAcrossM = 0.f;  // rank half-extent
		int32 AliveCount = 0;
		uint8 bGarrisoned = 0;
	};

	/** Emitted engagement record for one direction (attacker -> defender). */
	struct FEngagementRecordBuild
	{
		FUnitHandle Attacker;
		FUnitHandle Defender;
		uint8 Mode = 0;           // EEngagementMode
		float FrontageStart = 0.f;
		float FrontageEnd = 0.f;
		int32 DesiredPressure = 0;
	};

	/** Flood-fill connected components over the unit contact graph (spec
	 *  §4.2; nodes are indices into the unit array, pairs are contacting
	 *  index pairs). Deterministic: lowest-index seed order, ascending BFS. */
	MASSBATTLE_API void ClusterGraph(int32 NumNodes, const TArray<FIntPoint>& ContactPairs, TArray<int32>& OutClusterId);

	/** Builds the engagement record for one direction of a contacting pair.
	 *  Mode (spec §4.2): Mob when the defender's aggregated attacker directions
	 *  span >= 3 quadrants (DirectionMask bits: +X=1, -X=2, +Y=4, -Y=8) --
	 *  the isolated-defender mob case; else Frontal when the attacker stands
	 *  opposite the defender's frontage interior (|t| <= HalfAlong), Flank when
	 *  it wraps an end. Frontage = the attacker's along-frontage extent clipped
	 *  to the defender's line (meters); [0,0] for Mob. */
	MASSBATTLE_API void BuildEngagementRecord(const FUnitClusterNode& Attacker, const FUnitClusterNode& Defender,
		uint8 DirectionMask, FEngagementRecordBuild& Out);

	/** One assign pass over the mirrors (spec §4.4, elaborated): validation
	 *  and releases top-of-pass (immediate -- vacated slots are visible to
	 *  later seekers in the same tick, the <= 1 tick handoff EC5), then
	 *  unpaired seekers take targets under unit-level caps and per-defender
	 *  budgets. Mutates Seekers (targets/states/timers/backoff), the records'
	 *  AssignedAttackers, and the tally table. */
	MASSBATTLE_API void RunAssignPass(TArray<FAssignSeekerState>& Seekers,
		const TArray<FAssignDefenderState>& Defenders,
		TArray<FAssignRecordView>& Records,
		FCombatTallyTable& Tally,
		const FAssignContext& Context);
}
