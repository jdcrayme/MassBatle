// BattleSpatialHash.h -- uniform spatial hash over the battle tile (spec §3.3,
// arch §7.1; task 5). Game-thread rebuild each tick from end-of-last-tick
// transforms via a two-pass counting sort into preallocated arrays (EC9: zero
// per-tick allocation in the steady fight -- arrays grow at battle start
// only). QueryCircle serves COVERAGE and MOB TARGETING only -- formed-fight
// targeting reads defender slot tables (data, not proximity; arch §7.1):
// lanes never do circle queries.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Mass/EntityHandle.h"
#include "BattleTypes.h"
#include "BattleSpatialHash.generated.h"

/** One hash entry (spec §3.3): packed, blittable, sorted by cell. */
struct FBattleHashEntry
{
	FMassEntityHandle Entity;
	FUnitHandle Unit;
	/** ECombatHashFlags bits (currently none; reserved for ranged/LOD). */
	uint8 Flags = 0;
	/** Body radius, centimeters (fact 1.9: 40 cm infantry). */
	uint8 RadiusCm = 40;
	/** EBattleWeaponReach (loadout-stamped; ring + budget inputs). */
	uint8 ReachClass = 1;
};

UCLASS()
class MASSBATTLE_API UBattleSpatialHashSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// -- USubsystem --
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Preallocate for a battle scale (call once at battle start; later
	 *  rebuilds reuse the allocations, EC9). */
	void Reserve(int32 ExpectedEntries);

	/** Full rebuild from this tick's entries + their centers (game thread,
	 *  start of combat bookkeeping). The input is sorted handle-stable inside,
	 *  then counting-sorted into cells, so within-cell iteration order is
	 *  deterministic (spec §4.1). */
	void RebuildWithPositions(TArray<FBattleHashEntry>&& Entries, TArray<FVector2D>&& PositionsM);

	/** Position-less variant (tests/defaults: positions read as (0,0)). */
	void Rebuild(TArray<FBattleHashEntry>&& Entries);

	/** Circle query in METERS; appends entries whose positions are within
	 *  Radius of Center, with the parallel positions (the entry struct is
	 *  packed; consumers read positions here, never via a second lookup). */
	void QueryCircle(const FVector2D& CenterM, float RadiusM, TArray<FBattleHashEntry>& OutEntries,
		TArray<FVector2D>* OutPositionsM = nullptr) const;

	/** O(live entries): the handle-sorted entry array (debug/dump consumers). */
	const TArray<FBattleHashEntry>& GetSortedEntries() const { return SortedEntries; }

	/** Slot index of one entity in the sorted arrays (binary search);
	 *  INDEX_NONE when absent. */
	int32 FindSlotFor(FMassEntityHandle Entity) const;

	/** Entry position in meters (parallel array to SortedEntries). */
	const TArray<FVector2D>& GetEntryPositionsM() const { return EntryPositionsM; }

	// -- metrics (B.4) --
	double GetHashBuildMs() const { return HashBuildMs; }

	/** Grid parameters (m; matches the cost-grid footprint, 3 m cells). */
	static constexpr float CellSizeM = 3.0f;
	static constexpr float TileHalfExtentM = 128.0f; // 256 x 256 m tile

private:
	/** Deterministic cell id from world meters; -1 when outside the tile. */
	int32 CellIndexFor(const FVector2D& PosM) const;

	/** Tile origin (min corner), meters; fixed to the battle tile. */
	FVector2D OriginM = FVector2D(-TileHalfExtentM, -TileHalfExtentM);
	int32 DimCells = 0;

	/** Counting-sort scratch: per-cell counts + prefix offsets (preallocated). */
	TArray<int32> CellCounts;
	TArray<int32> CellOffsets;

	/** Rebuilt arrays: entries sorted by cell (handle-stable within a cell),
	 *  with positions parallel. Preallocated at Reserve; Rebuild writes in
	 *  place (SetNumUninitialized on already-reserved memory, EC9). */
	TArray<FBattleHashEntry> SortedEntries;
	TArray<FVector2D> EntryPositionsM;
	/** Cell start offsets into SortedEntries (DimCells + 1). */
	TArray<int32> CellStarts;

private:
	/** Input sort scratch (handle-sorted source, spec §4.1 determinism note;
	 *  the index permutation lives here too -- EC9, no per-tick allocation). */
	TArray<FBattleHashEntry> InputScratch;
	TArray<FVector2D> InputPositionsScratch;
	TArray<int32> OrderScratch;

	void RebuildImpl(TArray<FBattleHashEntry>&& Entries, TArray<FVector2D>&& PositionsM);

public:

	double HashBuildMs = 0.0;
};
