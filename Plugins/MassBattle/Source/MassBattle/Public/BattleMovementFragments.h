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
 *  (planner T1.9, path-frame layout T1.2) unchanged. Centimeters. */
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

/**
 * Per-unit movement broadcast channel (arch §3.1 row, written by UnitLocomotion).
 * One instance per unit; ADR-14 anchor first -- created through the
 * handle-seeded hash path exactly like FUnitStatsSharedFragment (E1).
 */
USTRUCT()
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

	/** Arc-length position of the anchor along Path (cm; UnitLocomotion-owned). */
	UPROPERTY() float PathS = 0.f;

	/** Order serial the locomotion stage last planned for (change detector;
	 *  registry OrderSerial is the source of truth). */
	UPROPERTY() uint32 PlannedOrderSerial = 0;

	/** T1.7 commit rule: the squeeze/morph decision locks once made for the
	 *  current pass -- no re-decision mid-corridor (no thrash). */
	UPROPERTY() uint8 PassLocked = 0;

	/** Planner path; empty (NumPoints<2) until UnitLocomotion plans one. */
	UPROPERTY() FBattlePathPolyline Path;
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
};

/** Servo target written by FormationSlotResolve (arch §4 row 6: "move targets").
 *  World-space slot position + per-rank facing, centimeters / degrees. */
USTRUCT()
struct FSlotTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY() FVector2D TargetPos = FVector2D::ZeroVector;
	UPROPERTY() float TargetYaw = 0.f;
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
};
