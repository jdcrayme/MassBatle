// BattleGarrisonTypes.h -- garrison data model (spec garrison-movement §3).
// Plain data on the seam: structure records / bookings are registry-side plain
// structs (never hot fragments -- arch §3.1); the enums + slot mirror travel
// inside FUnitFormationSharedFragment and are reflected for that reason.
// Units follow the project convention: records and seam math in METERS,
// fragment mirrors in CENTIMETERS (phase-1 ground rule 2).
#pragma once

#include "CoreMinimal.h"
#include "BattleTypes.h"
#include "BattleGarrisonTypes.generated.h"

/** Gray-box structure families of this phase (spec §1: building, wall, trench). */
UENUM(BlueprintType)
enum class EGarrisonStructureType : uint8
{
	Building,
	Wall,
	Trench
};

/** Per-unit garrison participation (spec §3.3). None = field unit. */
UENUM(BlueprintType)
enum class EGarrisonMode : uint8
{
	None,
	Garrisoning, // order taken, approaching / queueing / filing in
	Garrisoned,  // occupying or settled at posts
	Exiting      // filing out along the egress path
};

/** Garrison FSM phases (spec §4.2-4.5). GarrisonPhase is meaningful only when
 *  GarrisonMode != None. Settled = posts held (B7). */
UENUM(BlueprintType)
enum class EGarrisonPhase : uint8
{
	None = 0,
	Approach, // field A* to the entrance queue point
	Queue,    // structure full or lease busy: hold at queue point, retry
	Ingress,  // filing through the entrance on the internal path
	Occupy,   // distributing from the distribution node to booked posts
	Settled,  // every live entity at its post (hold)
	Egress    // filing out to the assembly point
};

/** Booking capacity per unit mirror (spec §3.3): fixture rosters are 40; 64
 *  covers a 160-man unit at 2.5 files. Fixed array = the blittability price. */
constexpr int32 MaxGarrisonSlotsPerUnit = 64;

/** Entrance corridor cost stamped over QueuePos->DoorPos cells (spec §3.1):
 *  high-but-passable so field A* prefers the door approach without
 *  special-casing the planner (ADR-19). */
constexpr uint8 GarrisonCorridorCost = 64;

/** One firing position (window, parapet, trench post). Record-side, METERS. */
struct FGarrisonSlot
{
	FVector2D Pos = FVector2D::ZeroVector; // m, planar
	float Z = 0.f;                         // m (wall top > 0, trench < 0, building floor = 0)
	float YawDeg = 0.f;                    // facing at the post (outward at parapets/windows)
	uint8 EntranceIndex = 0;               // which entrance routes to this slot
};

/** Arc-length path sample with elevation (spec §3.1): the seam-side internal
 *  path point. FBattlePathPoint (the fragment polyline) carries the same Z in
 *  centimeters so interior paths travel losslessly into workers. METERS. */
struct FBattlePathPoint3
{
	FVector2D Pos = FVector2D::ZeroVector; // m, planar
	float S = 0.f;                         // arc length from path start, m
	FVector2D Tangent = FVector2D(1.f, 0.f);
	float Z = 0.f;                         // m
};

/** One choke point + its exterior anchor positions. METERS. ClearWidthM has a
 *  documented minimum of 4 m: doorways narrower than a 3 m grid cell cannot be
 *  expressed geometrically (risk R-G2) -- filing width is declared, not
 *  measured (PassLocked pre-commit). */
struct FGarrisonEntrance
{
	FVector2D DoorPos = FVector2D::ZeroVector;   // m -- the choke point
	FVector2D QueuePos = FVector2D::ZeroVector;  // m -- exterior approach point (planner goal)
	FVector2D AssemblyPos = FVector2D::ZeroVector; // m -- exterior re-form point after egress
	/** Distribution node (spec §3.1/§4.3): interior point soldiers spread out
	 *  from; Z here is the interior elevation (wall walk / channel floor). */
	FVector2D DistributionPos = FVector2D::ZeroVector;
	float DistributionZ = 0.f;
	float ClearWidthM = 4.f;                     // declared choke width (drives column morph)
};

/** Immutable-after-registration structure record (ground rule 6). Assigned
 *  StructureId is the order payload handle. METERS. */
struct FGarrisonStructureRecord
{
	EGarrisonStructureType Type = EGarrisonStructureType::Building;
	int32 MaxUnits = 1;             // booking capacity (ETW tooltip parity, B2)
	FBox2D Footprint = FBox2D(EForceInit::ForceInit); // m -- stamped impassable
	TArray<FGarrisonEntrance> Entrances;
	/** Per entrance: QueuePos -> DoorPos -> distribution node, arc-length
	 *  parameterized, carries Z (entry direction). Egress is the reverse walk
	 *  ending at AssemblyPos (built by FGarrisonLayout::BuildInternalPath). */
	TArray<TArray<FBattlePathPoint3>> EntryPaths;
	TArray<FGarrisonSlot> Slots;
};

/** Plain booking row (spec §3.2). Disjoint slot range [SlotBase, SlotBase+SlotCount). */
struct FGarrisonBooking
{
	FUnitHandle Unit;
	uint32 StructureId = 0;
	uint8 EntranceIndex = 0;
	int32 SlotBase = 0;
	int32 SlotCount = 0;
};
