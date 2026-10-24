// BattleCostGridSubsystem.h -- per-world owner of the movement cost grid
// (arch §4 row 1 consumers: UnitLocomotion reads it; A5 test stamps write it).
// The grid itself is the pure-math FBattleCostGrid (meters); the subsystem is
// game-thread-only. Default tile: flat 256x256 m centered on the origin,
// 3 m cells (arch §12).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BattleCostGrid.h"
#include "BattleCostGridSubsystem.generated.h"

UCLASS()
class MASSBATTLE_API UBattleCostGridSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// -- UWorldSubsystem --
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** The movement cost grid (game thread; processors copy what they need or
	 *  read via the game-thread locomotion stage). */
	const FBattleCostGrid& GetGrid() const { return Grid; }
	FBattleCostGrid& GetMutableGrid() { return Grid; }

	/** A5 stamp override (meters, world space). Idempotent. */
	int32 StampImpassable(const FVector2D& BoxMinMeters, const FVector2D& BoxMaxMeters);

	/** Re-plan helper for deterministic tests: resets to the flat baseline. */
	void ResetToFlat() { Grid.ResetToFlat(); }

private:
	FBattleCostGrid Grid;
};
