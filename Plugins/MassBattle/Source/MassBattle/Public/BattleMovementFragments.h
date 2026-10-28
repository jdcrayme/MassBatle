// BattleMovementFragments.h -- Phase 1 movement data spine (arch §3.1 rows +
// §8 movement doctrine, decision A1). All positions/velocities in engine units
// (centimeters) -- project math is meters; convert at the fragment boundary
// only (phase-1 ground rule 2, the E2 bug class). Hot fragments stay
// blittable: fixed C arrays, no FString/UObject*/TArray (arch §3.1).
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityElementTypes.h"
#include "MassMovementFragments.h" // FMassVelocityFragment (adoption per arch §3.1 note, engine-api §14)
#include "BattleTypes.h"
#include "BattleGarrisonTypes.h"
#include "BattleMovementFragments.generated.h"

/** Entity movement states (arch §8.3 state table, all six rows; PressRing /
 *  Charge / Flee arrive with their phases). Enum-in-fragment is the arch §3.2
 *  "acceptable fallback" form for combat-mode switching. */
UENUM(BlueprintType)
enum class EBattleMovementState : uint8
{
	HoldSlot,       /**< formed, servoing to slot; no avoidance (crowding is servo+compression) */
	ManeuverToSlot, /**< displaced from slot, re-entering formation; medium teammates-only avoidance */
	CrossField,     /**< traveling across the field; full RVO + asymmetric tie-break */
	PressRing,      /**< at the contact ring (Phase 2+) */
	Charge,         /**< charging (Phase 2+) */
	Flee            /**< routing (Phase 4+) */
};

/** Path-polyline sample (arc-length parameterized). Plain reflected struct so
 *  it can live inside a shared fragment and feed the pure-math seam functions
 *  (planner T1.9, path-frame layout T1.2) unchanged. Centimeters. Z carries
 *  elevation on garrison internal paths (spec §6); field paths keep Z = 0
 *  bit-for-bit (the Phase 1 straight-path degeneracy is preserved). */
USTRUCT()
struct FBattlePathPoint
{
	GENERATED_BODY()

	/** World position (cm, planar -- flat tile in Phase 1). */
	UPROPERTY() FVector2D Pos = FVector2D::ZeroVector;

	/** Arc length from the path start (cm), monotonically nondecreasing. */
	UPROPERTY() float S = 0.f;

	/** Unit tangent at this sample (planar, normalized). */
	UPROPERTY() FVector2D Tangent = FVector2D(1.f, 0.f);

	/** Elevation at this sample (cm; garrison internal paths only, else 0). */
	UPROPERTY() float Z = 0.f;
};

/** Max samples per unit path. A 3 m-grid planner path over the Phase 1 tile
 *  needs < 30 samples after smoothing; 64 leaves slack without unbounded data.
 *  Fixed size is the blittability price (arch §3.1: no TArray in fragments). */
constexpr int32 MaxBattlePathPoints = 64;

/** Arc-length sampled path polyline owned by a unit (consumed by FormationSlotResolve
 *  on workers, so it must travel inside the shared fragment, not the registry). */
USTRUCT()
struct FBattlePathPolyline
{
	GENERATED_BODY()

	UPROPERTY() int32 NumPoints = 0;
	UPROPERTY() float TotalLength = 0.f; // cm
	UPROPERTY() FBattlePathPoint Points[MaxBattlePathPoints];

	bool IsValid() const { return NumPoints >= 2; }
};

/** Garrison post mirror entry (A2): one booked-range slot copied game-thread
 *  into the shared fragment at Occupy. Centimeters (fragment boundary). */
USTRUCT()
struct FGarrisonSlotFragment
{
	GENERATED_BODY()

	UPROPERTY() FVector2D Pos = FVector2D::ZeroVector; // cm, planar
	UPROPERTY() float Z = 0.f;                         // cm (wall top > 0, trench < 0)
	UPROPERTY() float YawDeg = 0.f;                    // facing at the post
};

/**
 * Per-unit movement broadcast channel (arch §3.1 row, written by UnitLocomotion).
 * One instance per unit; ADR-14 anchor first -- created through the
 * handle-seeded hash path exactly like FUnitStatsSharedFragment (E1).
 */USTRUCT()
struct FUnitFormationSharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	// ADR-14 uniqueness anchor -- keep first.
	UPROPERTY() FUnitHandle OwnerUnit;

	/** Formation anchor (cm, planar) -- the locomoting reference point. */
	UPROPERTY() FVector2D AnchorPos = FVector2D::ZeroVector;

	/** Commanded facing, degrees. */
	UPROPERTY() float FacingYaw = 0.f;

	/** Unit speed limit from the §8.2 Min3 (cm/s). */
	UPROPERTY() float SpeedLimit = 0.f;

	/** Compression scale on layout offsets (§8.2: floor 60%, recovery ~10%/s). */
	UPROPERTY() float SpacingScale = 1.f;

	/** Bumped on every anchor/facing/path/layout re-decision; per-entity slot
	 *  caches compare against it to detect staleness. */
	UPROPERTY() int32 Generation = 1;

	/** Generation the slot assigner last ran under (FormationSlotResolve).
	 *  A mismatch = layout re-decision (order/morph/refront) and forces an
	 *  immediate UNBOUNDED locality re-assign, so refronts remap each entity
	 *  to its nearest new slot instead of a k-bounded shuffle across the block. */
	UPROPERTY() int32 AssignedGeneration = 0;

	// -- formed layout parameters (§8.6 path-frame layout) --

	/** Entities per rank (files across). 0 = layout not yet initialized. */
	UPROPERTY() int32 NumFiles = 0;
	/** Original grid size (spawn count): slots stay 0..NumSlots-1 across
	 *  casualties so reflow fills gaps without shrinking the frontage. */
	UPROPERTY() int32 NumSlots = 0;
	/** Rank depth spacing (cm). */
	UPROPERTY() float RankSpacing = 0.f;
	/** File lateral spacing (cm). */
	UPROPERTY() float FileSpacing = 0.f;

	/** Batched-reflow cadence state (arch §8.4; FReflowScheduler mirror,
	 *  worker-written). */
	UPROPERTY() float ReflowCooldown = 0.f;

	/** Phase 2 B1 (F-6): max over live entities of dist(pos, written move
	 *  target) in METERS, reduced by FormationSlotResolve (worker) and read
	 *  game-thread by the UnitLocomotion straggler pace term + diagnostics.
	 *  Same cross-thread pattern as ReflowCooldown above. Diagnostic: never
	 *  hashed. */
	UPROPERTY() float MaxSlotErrorM = 0.f;

	/** Straggler-pace term (2026-10-28): MEAN over live entities of
	 *  dist(pos, written move target), same reduction sites as MaxSlotErrorM.
	 *  The pace throttle reads THIS, not the max: a commanded re-front
	 *  (S4_WidthOverride / S5_MarchRefront) sends edge files on 12-18 m
	 *  lateral walks while the block's center re-dresses in place -- max
	 *  error floored the anchor at 0.25x for the whole walk and burned the
	 *  march budgets in stop-wait-crawl cycles (the owner's own errata note
	 *  in StragglerPaceFactor documents the stop-wait-crawl family). A real
	 *  drag or choke queue moves the BLOCK -- the mean crosses DressBreak
	 *  only then. Same cross-thread pattern; never hashed. */
	UPROPERTY() float MeanSlotErrorM = 0.f;

	/** Phase 2 C: one-shot gate for the SCHEDULED final assignment (Pottinger
	 *  scheduled slot positioning). 0 = the march assignment (greedy,
	 *  march-coherent) owns the unit; the first resolve tick where the blend
	 *  engages runs AssignFinal ONCE (crossing-free final approaches) and sets
	 *  this. Cleared at every order consumption. Never hashed. */
	UPROPERTY() uint8 bFinalAssignDone = 0;

	/** Arc-length position of the anchor along Path (cm; UnitLocomotion-owned). */
	UPROPERTY() float PathS = 0.f;

	/** Order serial the locomotion stage last planned for (change detector;
	 *  registry OrderSerial is the source of truth). */
	UPROPERTY() uint32 PlannedOrderSerial = 0;

	/** T1.7 commit rule: the squeeze/morph decision locks once made for the
	 *  current pass -- no re-decision mid-corridor (no thrash). */
	UPROPERTY() uint8 PassLocked = 0;

	/** Morph decisions taken in the CURRENT traversal (audit H2/H5: the
	 *  no-thrash contract is per order serial; reset with PassLocked on a new
	 *  order, asserted <= 1 by S3). Diagnostic counter, never hashed. */
	UPROPERTY() uint8 PassMorphCount = 0;

	/** Planner path; empty (NumPoints<2) until UnitLocomotion plans one. */
	UPROPERTY() FBattlePathPolyline Path;

	// -- TW fan-in destination frame (A2 game-thread mirror; workers read) --
	// Armed ONLY by a successful field-Move plan (UnitLocomotion order
	// consumption) and cleared on every other order consumption, so garrison
	// approach/filing paths never inherit a stale destination. While armed and
	// a path exists, FormationSlotResolve offers each entity its
	// destination-layout slot as an alternative target and the entity peels to
	// it whenever that is the closer walk (Total War-style fan-in) instead of
	// holding the rigid marching block until anchor arrival.

	/** Destination anchor (cm, planar) -- the planned polyline's last point. */
	UPROPERTY() FVector2D FinalAnchorPos = FVector2D::ZeroVector;
	/** Destination facing (degrees) -- the ordered facing applied at arrival. */
	UPROPERTY() float FinalFacingYaw = 0.f;
	/** 1 = FinalAnchorPos/FinalFacingYaw describe the current field move. */
	UPROPERTY() uint8 FanInDestValid = 0;

	// -- garrison state (spec §3.3; ADR-18: fragment fields, no new tags /
	//    processors / archetypes). Written game-thread by UnitLocomotion;
	//    worker processors read these mirrors exclusively (A2). --

	/** Field-layout file count at ingress entry (restored on egress). */
	UPROPERTY() int32 FieldFilesSaved = 0;
	/** Garrison participation + FSM phase (meaningful when Mode != None). */
	UPROPERTY() EGarrisonMode GarrisonMode = EGarrisonMode::None;
	UPROPERTY() EGarrisonPhase GarrisonPhase = EGarrisonPhase::None;
	/** Structure + entrance the unit is committed to (0 / 0 when None). */
	UPROPERTY() uint32 GarrisonStructureId = 0;
	UPROPERTY() uint8 GarrisonEntranceIndex = 0;
	/** Booked slot range [GarrisonSlotBase, GarrisonSlotBase+GarrisonSlotCount),
	 *  mirrored from the ledger at Occupy (A2). */
	UPROPERTY() int32 GarrisonSlotBase = 0;
	UPROPERTY() int32 GarrisonSlotCount = 0;
	/** Current interior elevation target (cm; Z servo input, spec §6). */
	UPROPERTY() float GarrisonZ = 0.f;
	/** Queue/egress-retry cadence state (A4; same pattern as ReflowCooldown,
	 *  F16: persisted here so it survives chunk boundaries). */
	UPROPERTY() float GarrisonRetryCooldown = 0.f;
	/** Metric timestamps (GFrameNumber at order issue / queue entry; B.8 item
	 *  4 diagnostics only -- never hashed, never sim-authoritative). */
	UPROPERTY() int32 GarrisonOrderTick = 0;
	UPROPERTY() int32 GarrisonQueueTick = 0;
	/** Booked-range mirror: posts in cm (fixed array; not touched until
	 *  Occupy). Indexed by (slot - GarrisonSlotBase). */
	UPROPERTY() FGarrisonSlotFragment Slots[MaxGarrisonSlotsPerUnit];
};

/** Per-entity slot assignment (arch §3.1 row: slot index + generation counter;
 *  Generation mirrors FUnitFormationSharedFragment::Generation when in sync). */
USTRUCT()
struct FSlotAssignmentFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Linear slot id: rank * NumFiles + file (rank 0 = front). */
	UPROPERTY() int32 SlotIndex = INDEX_NONE;
	UPROPERTY() int32 Generation = 0;

	/** Phase 2 C3 handoff latch: 1 once the entity came within the handoff
	 *  distance of its final slot (its target is then locked to the final
	 *  slot -- the early TW settle). Cleared whenever the layout re-assigns
	 *  (new slots, old latch is meaningless). Diagnostic: never hashed. */
	UPROPERTY() uint8 bFinalLocked = 0;

	/** Formation-flow rule 2 (owner proposal 2026-10-28): set by ServoProcessor
	 *  when the unstuck ladder reaches Battle.Movement.StuckReawardLevel (the
	 *  slot is physically unreachable -- the entity is blocked out and orbiting
	 *  the block); FormationSlotResolve re-homes marked entities at their
	 *  nearest reachable slot on the next reflow. ServoProcessor runs after the
	 *  resolve stage in the frame chain, so the mark carries one frame of lag --
	 *  invisible at the 0.5 s reflow cadence. Diagnostic byte, never hashed
	 *  (bFinalLocked pattern); Level 0 recovery clears it. */
	UPROPERTY() uint8 StuckMark = 0;
};

/** Servo target written by FormationSlotResolve (arch §4 row 6: "move targets").
 *  World-space slot position + per-rank facing, centimeters / degrees. TargetZ
 *  carries elevation for the garrison Z servo (spec §6): structure-slot Z when
 *  occupying, path-interpolated Z while filing; 0 in the field. */
USTRUCT()
struct FSlotTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY() FVector2D TargetPos = FVector2D::ZeroVector;
	UPROPERTY() float TargetYaw = 0.f;
	UPROPERTY() float TargetZ = 0.f;

	/** Phase 2 A1 feed-forward: the target as of the PREVIOUS resolve tick,
	 *  written by FormationSlotResolve before the overwrite. The servo forms
	 *  the slot's own velocity from (Target - Prev)/DT and cancels the
	 *  systematic Tau*v trailing lag of a pure position pursuit. bPrevValid
	 *  gates the first write (an unvalid Prev would read as a huge one-frame
	 *  slot velocity); diagnostic bytes, never hashed. */
	UPROPERTY() FVector2D PrevTargetPos = FVector2D::ZeroVector;
	UPROPERTY() uint8 bPrevValid = 0;
};

/** Movement state (§8.3 enum; per-entity -- formation behavior is read per
 *  entity by the servo, unit-wide transitions fan out via UnitLocomotion). */
USTRUCT()
struct FMovementStateFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY() EBattleMovementState State = EBattleMovementState::HoldSlot;
};

/** Servo + unstuck-ladder per-entity state (arch §8.5). LadderLevel is the
 *  A3 overlay hook datum (0 = none, 1..4 = ladder steps). */
USTRUCT()
struct FServoStateFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Seconds accumulated with speed < eps while not near slot (§8.5 trigger). */
	UPROPERTY() float LowSpeedTime = 0.f;
	/** Unstuck ladder level 0..4 (§8.5 steps; 0 = not stuck). */
	UPROPERTY() int32 LadderLevel = 0;
	/** Sidestep direction while on ladder level 2 (unit planar vector). */
	UPROPERTY() FVector2D SidestepDir = FVector2D::ZeroVector;
	/** Displacement blend weight for shove/wake (Task 9; 0 = none). */
	UPROPERTY() float DisplacementWeight = 0.f;
	/** Audit M1, ladder step 4: the terminal handoff flag -- set when the
	 *  ladder reaches level 4, cleared on full recovery. The unit-AI layer
	 *  (Phase 2) consumes it; nothing else keys on it. */
	UPROPERTY() uint8 bUnitAIHandoff = 0;
};
