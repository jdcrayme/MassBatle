// BattleGarrison.h -- garrison pure math on the B.0 seam (phase ground rule 1).
// ALL functions are free functions / plain structs, deterministic, no RNG, no
// wall clock, METERS. Processors stay thin shims: read fragments/records ->
// call these -> write fragments. (spec garrison-movement §3-§4, tasks 2-3)
#pragma once

#include "CoreMinimal.h"
#include "BattleGarrisonTypes.h"

struct FBattleCostGrid;

/** Battle.Garrison.* CVAR block (brief task 0; B.8 item 6). Defined in
 *  BattleGarrison.cpp; read through these accessors from any TU. */
namespace FGarrisonCVars
{
	// 0 until the locomotion FSM lands (task 5) so the T1.8 baseline stays
	// byte-identical while the data spine is in flight; 1 from task 5 on.
	MASSBATTLE_API int32 Enable();
	// Interior speed clamp factor for Ingress/Egress (A3; default 0.5).
	MASSBATTLE_API float InteriorSpeedFactor();
	// Queue retry cadence, seconds (A4; default 0.5).
	MASSBATTLE_API float QueueRetry();
	// Garrison reflow displacement bound, meters (default 3.0).
	MASSBATTLE_API float MaxSlotDisplacement();
	// Z servo rate bound, m/s (A8; default 2.0).
	MASSBATTLE_API float MaxZRate();
	// Soak garrison-order mix (task 8; default 0 = T1.8 baseline untouched).
	MASSBATTLE_API int32 SoakOrders();
}

namespace FGarrisonLayout
{
	/** Gray-box fixture records (task 2; F15 -- C++-authored, no editor
	 *  actors). Builds the record for one structure of the given type centered
	 *  at PosMeters, rotated by YawDeg. Deterministic geometry per type
	 *  (building floor Z = 0, wall top Z = 6, trench channel Z = -0.75;
	 *  ClearWidthM = 4 per R-G2). Returns false on a bad type. */
	MASSBATTLE_API bool MakeFixture(EGarrisonStructureType Type, const FVector2D& PosMeters, float YawDeg,
		FGarrisonStructureRecord& OutRecord);

	/** Cost-grid stamping for a record (task 2): footprint cells impassable,
	 *  entrance corridor cells (QueuePos->DoorPos boxes) at GarrisonCorridorCost.
	 *  Deterministic + idempotent; returns cells stamped. */
	MASSBATTLE_API int32 StampStructure(const FGarrisonStructureRecord& Record, FBattleCostGrid& Grid);

	/** Internal path for one entrance (task 3; spec §4.3/§4.5). Entry direction:
	 *  QueuePos -> DoorPos -> distribution node. Egress: node -> DoorPos ->
	 *  AssemblyPos (ground rule 5: same builder, direction flag). Arc-length
	 *  parameterized, Z monotone across stair/ramp segments, endpoints exact. */
	MASSBATTLE_API bool BuildInternalPath(const FGarrisonStructureRecord& Record, int32 EntranceIndex,
		bool bEgress, TArray<FBattlePathPoint3>& OutPath);

	/** Deterministic greedy nearest-vacant bijection over the slot set (task 3;
	 *  the k-file bound's analog): soldiers bid in roster order, ties to the
	 *  lower slot index; candidates farther than MaxSlotDisplacementM from the
	 *  soldier's previous slot are skipped unless nothing else is vacant.
	 *  OutAssignment[SoldierIdx] = SlotIdx. */
	MASSBATTLE_API void AssignSlots(const TArray<FVector2D>& SoldierPositionsM, const TArray<FVector2D>& SlotPositionsM,
		const TArray<int32>& PreviousAssignment, float MaxSlotDisplacementM, TArray<int32>& OutAssignment);

	/** Queue hold points for up to MaxQueued waiting units (task 3; spec §5):
	 *  offsets behind QueuePos along the approach normal (away from the door),
	 *  index 0 = QueuePos itself. */
	MASSBATTLE_API void QueuePoints(const FGarrisonStructureRecord& Record, int32 EntranceIndex,
		int32 MaxQueued, TArray<FVector2D>& OutPointsM);

	/** Z interpolation along a seam path at arc length S (clamped); monotone on
	 *  stair/ramp fixtures (T-G.5). */
	MASSBATTLE_API float ZAtS(const TArray<FBattlePathPoint3>& Path, float S);

	/** Fragment-boundary twin (cm in / cm out): Z interpolation along the
	 *  fragment polyline at arc position SCm -- filed soldiers' TargetZ while
	 *  the path carries elevation (spec §6). */
	MASSBATTLE_API float ZAtSFragment(const struct FBattlePathPolyline& PathCm, float SCm);

	/** Ingress column count clamp (task 3; spec §4.3): clamp(floor(ClearWidth /
	 *  FileSpacing), 1, NumFilesUpperBound). */
	MASSBATTLE_API int32 ColumnFilesFor(float ClearWidthM, float FileSpacingM, int32 NumFilesUpperBound);
}
