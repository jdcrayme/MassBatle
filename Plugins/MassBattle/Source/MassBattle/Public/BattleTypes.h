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

/** The deterministic hash family's multipliers (ONE home, the B-2 rule):
 *  every seed-derived draw in the project multiplies with these -- the melee
 *  staggers/rolls (FMeleeMath), the ballistics shot seeds (FBallisticsMath),
 *  and the A13 canonical entity keys below. Same values everywhere; a
 *  per-file retyped 2654435761u drifts silently and the two-run equality
 *  (RC8) is the property that silently dies. Rename, never a re-mix: the
 *  expressions at each site are unchanged, only their constant's home. */
namespace BattleHashFamily
{
	/** Knuth golden-ratio multiplier (2654435761u). */
	constexpr uint32 GoldenRatio = 2654435761u;
	/** First auxiliary mix (40503u). */
	constexpr uint32 MixA = 40503u;
	/** Second auxiliary mix (2685821657u). */
	constexpr uint32 MixB = 2685821657u;
}

/** A13 canonical per-entity identity (Phase 3, RC8): a deterministic
 *  function of the owning unit + the roster slot -- INVARIANT across
 *  entity-slot churn. Mass reuses slots with new generations after a battle
 *  reset, and every hash-family draw keyed on raw FMassEntityHandle values
 *  permuted across the two-run equality (measured 2026-10-29: reload
 *  staggers, shot seeds, and combat rolls all diverged). One home; the
 *  stagger/seed/roll call sites feed these instead of handle parts. */
inline uint32 BattleCanonicalEntityKey(const FUnitHandle& Unit, int32 RosterIndex)
{
	uint32 H = Unit.Index * BattleHashFamily::GoldenRatio;
	H ^= Unit.Serial * BattleHashFamily::MixA;
	H ^= static_cast<uint32>(RosterIndex) * BattleHashFamily::MixB;
	H ^= H >> 15;
	return H;
}

/** The roll/stagger count input: the per-entity count in the low 16 bits,
 *  the roster slot above it -- one uint32, run-stable, collision-free
 *  within a battle (rosters are <= 4096). */
inline uint32 BattleCanonicalCount(int32 RosterIndex, uint32 Count)
{
	return (static_cast<uint32>(RosterIndex) << 16) | (Count & 0xFFFFu);
}

/** Soldier body geometry (ONE home, fact 1.9's 40 cm body + the phase-3
 *  ranged heights): every body-size read in the project consumes these --
 *  the engagement contact radius (aliased as FCombatSolver::Contact::
 *  BodyRadiusM), the ranged lane/impact corridors, and the height gates.
 *  A per-file retyped 0.4f/1.6f/1.8f drifts silently (the phase-2 audit's
 *  B-1 lesson, re-scattered by phase 3's ranged seam -- this audit's A-1). */
namespace BattleBody
{
	/** Body radius, meters (fact 1.9: FAgentRadiusFragment 40 cm). */
	constexpr float RadiusM = 0.4f;
	/** Launch/eye height, meters (the ranged fire pass's A6 placeholder). */
	constexpr float EyeHeightM = 1.6f;
	/** Body top, meters (the impact height gates' body ceiling). */
	constexpr float TopM = 1.8f;
	/** Aim-point height above ground, meters (the volley target's body center). */
	constexpr float AimCenterM = 1.0f;
}

/** Weapon reach class (spec §4.5: >= Spear grants the second-rank budget
 *  bonus; arch §9.1 spear reach). Mapped from the EDU stat_pri tech type at
 *  spawn (task 2 step 1); the class SET and mapping are A6 placeholders the
 *  Phase-6 T6.1 audit owns -- the ordering (>= Spear) is the load-bearing
 *  structure. Lives on the spatial-hash entry as uint8 (spec §3.3). */
enum class EBattleWeaponReach : uint8
{
	Short = 0,
	Normal = 1,
	Spear = 2,
	LongSpear = 3,
	Pike = 4
};

/** Projectile class (spec ranged-stack §3.1): the ranged discriminator is a
 *  fragment VALUE, not a composition tag -- bow and crossbow units share one
 *  ranged-capable composition (archetype discipline, spec §3.1); the profile
 *  table (BattleRanged.h) keys speed/dispersion/AP posture off this enum. */
UENUM(BlueprintType)
enum class EBattleProjectileKind : uint8
{
	None = 0,
	Arrow = 1,   // arced: indirect-capable, fires over a friendly frontage
	Bolt = 2     // flat: high-speed solution, lane-gated (spec §4.2)
};
/**
 * Unit discipline (spec morale-and-gas §3.3; EDU stat_mental[1]): scales
 *  BREAK RESISTANCE and the RALLY factor, never the contribution weights
 *  (MC2) -- an impetuous unit breaks as easily, rallies worse, and
 *  auto-charges (MC9); a disciplined unit absorbs more before breaking.
 *  Parsed loud on unknown tokens (the B-3 posture). */
enum class EBattleDiscipline : uint8
{
	Low = 0,
	Normal = 1,
	Disciplined = 2,
	Impetuous = 3
};

/** Unit training (EDU stat_mental[2]): formation neatness -- the movement
 *  polish consumer is future work; rides row stats inert this phase (the
 *  F4 slot pattern). */
enum class EBattleTraining : uint8
{
	Untrained = 0,
	Trained = 1,
	HighlyTrained = 2
};

/** Rout FSM state (spec morale-and-gas §3.1/§4.4, MC5): Steady above the
 *  wavering threshold; Wavering between the thresholds (combat continues,
 *  the save/UI-visible warning state); Routing once broken (flee order +
 *  pair mass-release); Shattered at the rally-exhaustion limit (flees
 *  forever, never rallies). Persisted (save v3); the flee bookkeeping
 *  around it is transient (the RC7 restore posture). */
enum class EBattleRoutState : uint8
{
	Steady = 0,
	Wavering = 1,
	Routing = 2,
	Shattered = 3
};

/** The rout apply's decision (PR-12, A7 -- the RC7 apply generalized): Flee
 *  issues the auto-flagged Move directly away from the threat anchor;
 *  RallyStop issues the auto-flagged Stop (pairs were released at the
 *  break). Kept int at the registry API (the ESkirmishDecision precedent). */
enum class EBattleRoutDecision : uint8
{
	Flee = 0,
	RallyStop = 1
};


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

	// Phase 2 (spec §3.2): unit-level engagement aggregates (FSM + metrics
	// inputs), rebuilt by the 4 Hz cluster pass. Game thread only.
	int32 EngagedCount = 0;
	int32 ChargingCount = 0; // written by the charge processor (task 7)

	// Phase 3 (spec ranged-stack §3.2): unit-level fire-control toggles + the
	// out-of-ammo aggregate flip. bFireAtWill defaults on; bSkirmish defaults on
	// for class-missile rows at spawn (A8); bOutOfAmmo flips exactly once when
	// the entity ammo sum hits zero (RC5, A14) and never un-flips (no resupply
	// v1). Registry APIs land with the Phase 3 data-spine task; the fields are
	// plain struct state so the save round-trip captures them verbatim.
	bool bFireAtWill = true;
	bool bSkirmish = false;
	bool bOutOfAmmo = false;

	// Phase 3 (spec §3.2/§3.5): the ammo-fraction denominator inputs, stamped
	// once at spawn. RangedInitialAmmo mirrors the row's per-entity
	// InitialAmmo (the fragment stays the per-entity authority; this is the
	// unit-aggregate copy save v2 carries so the denominator survives a
	// registry-only restore). InitialRosterSize stamps at the spawn fixup --
	// the live roster shrinks with deaths, the denominator must not. 0 on
	// either side reads as non-ranged (fraction -1).
	int32 RangedInitialAmmo = 0;
	int32 InitialRosterSize = 0;

	// Phase 3 (RC7, A8): skirmish auto-retreat state. TRANSIENT by design --
	// not in the save schema (a restored battle resumes auto-retreat
	// evaluation from the idle state). bRetreating is true while the unit's
	// current order is OUR retreat move; AutoRetreatSerial is the OrderSerial
	// at issue -- a player order bump changes the serial, which cancels the
	// retreat (the RC7 order-serial guard; the registry order APIs never
	// touch these two fields). RetreatingFireTarget remembers the attack
	// target the retreat's Move order displaced -- RC7's "then resumes fire"
	// needs the aim to survive the movement order (the fire pass reads it as
	// a target fallback; cleared when a player order supersedes).
	bool bRetreating = false;
	uint32 AutoRetreatSerial = 0;
	FUnitHandle RetreatingFireTarget;

	// PR-4 (audit A-2 root): locomotion's per-unit tick bookkeeping, registry
	// per-unit state instead of function-local static maps -- cleared with the
	// battle (ResetBattleState resets the whole Units map) and per-world
	// instead of per-process. ContactProbeTicks drives the pass-through probe
	// cadence (first 3 ticks, then every 240th); bPaceThrottled is the
	// StragglerThrottle event-edge latch (one log line per engagement/recovery).
	int32 ContactProbeTicks = 0;
	bool bPaceThrottled = false;

	// Phase 4a (spec morale-and-gas §3.1): the morale/fatigue/rout block.
	// UNIT-LEVEL by design (ADR-32: no per-entity morale fragment); the
	// registry owns the FSM + the sim mirror, the GAS Morale/Fatigue
	// attributes are written only by the exec calc (A14) and read back
	// through the snapshot path. LastMoraleTickSimTime is pipeline state
	// (excluded from the hash, the dirty-flag precedent) and drives the T4.8
	// staleness bound; -1 = never ticked. ExertionIntegral is monotone this
	// phase (no rest model -- spec §4.3). RoutCount drives the rally decay
	// + the Shattered lockout (MC6). The flee order bookkeeping below is
	// TRANSIENT like the skirmish retreat's -- a restored battle re-evaluates
	// from idle (spec §3.5).
	EBattleRoutState RoutState = EBattleRoutState::Steady;
	uint8 RoutCount = 0;
	float ExertionIntegral = 0.f;
	float FreshCasualtySpike = 0.f;
	float LastMoraleValue = 0.f;
	float LastFatigueValue = 0.f;
	float LastMoraleTickSimTime = -1.f;
	bool bGuardMode = false;
	EBattleDiscipline Discipline = EBattleDiscipline::Normal;
	/** First officer roster index (officers are the LAST OfficerCount
	 *  entities of the roster, spec §3.1/ADR-35); INDEX_NONE = none. The
	 *  death path resolves officer-ness from the dying entity's roster
	 *  index against this base in O(1). */
	int32 OfficerRosterBase = INDEX_NONE;
	int32 OfficerCount = 0;
	/** True while the unit's current order is OUR rout flee (the RC7
	 *  bRetreating pattern generalized); a player order-serial bump cancels
	 *  it (re-evaluates next tick). */
	bool bRoutFleeing = false;
	uint32 RoutFleeSerial = 0;
	/** Rally attempts so far in the current rout (the roll's count input,
	 *  A13; resets on rally or on a new break). */
	uint32 RallyAttempts = 0;

	// -- PR-12 morale-pass transient bookkeeping (NOT saved, NOT hashed --
	// the LastMoraleTickSimTime class; a restored battle re-derives) --
	/** Decaying recent-loss window for the WinningLocally term (spec 4.2;
	 *  NotifyMoraleCasualty feeds it). The KILLS half of the window has no
	 *  game-thread feed yet (the resolution pass runs worker-side; kill
	 *  attribution is a disclosed v1 gap) -- the field exists so the feed
	 *  lands without a layout change. */
	float DeathWindow = 0.f;
	float KillWindow = 0.f;
	/** The nearest enemy envelope anchor at the last fold (centimeters) --
	 *  the flee order's "directly away from the nearest threat" anchor. */
	FVector2D LastThreatAnchorCm = FVector2D::ZeroVector;
	bool bThreatValid = false;

	/** Impetuous auto-charge bookkeeping (A11/MC9; transient like the flee
	 *  bookkeeping): true while the current order is OUR auto-charge
	 *  AttackUnit; the serial check cancels on a player bump (re-issues only
	 *  onto idle or its own charge, never over a player order). */
	bool bAutoCharging = false;
	uint32 AutoChargeSerial = 0;

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

	/** Side id (Phase 3, A7/A8): the ranged pass's friend/enemy discriminator
	 *  (same team = friendly bodies; different team = skirmish threats). The
	 *  melee solver stays intent/record-driven and never reads it (A12).
	 *  Reality note: the spec's A7 wording presumes sides the data model
	 *  never defined -- this field is the minimal honest addition, disclosed
	 *  in the phase progress log (AGENTS 2.1). Default 0. */
	uint8 TeamId = 0;

	/** Officer count (Phase 4a, spec morale-and-gas §3.3/ADR-35): 0 = the
	 *  EDU row's own officer line count; >0 overrides (clamped to 3, the
	 *  schema's MaxOccurrences). Officers are EXTRA roster entities
	 *  (SoldierCount + OfficerCount) stamped bOfficer at fixup; their
	 *  front-center placement rides the layout seam (PR-12). */
	int32 OfficerCount = 0;

	/** Army general flag (Phase 4a, spec §3.3/A6): the unit's officer slot
	 *  0 is the general ENTITY -- its death raises
	 *  Battle.Team.GeneralDied, stamps the team shock term, and removes the
	 *  aura contribution (MC8). Default false. */
	bool bIsGeneral = false;

};
