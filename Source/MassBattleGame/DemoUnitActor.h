// DemoUnitActor.h -- level-placed unit for the demo (editor-authored
// scenarios). Thin subclass of the plugin's AUnitActor: adds the demo-side
// faction and an editor-only formation footprint preview (frontage line,
// depth box, slot dots) so scenario authors can lay out armies spatially.
// The actor's transform IS the spawn transform (position = formation anchor /
// front-rank center, yaw = facing) -- AUnitActor::SpawnEntities reads it.
#pragma once

#include "CoreMinimal.h"
#include "UnitActor.h"
#include "DemoTypes.h"
#include "DemoUnitActor.generated.h"

UCLASS(Blueprintable)
class MASSBATTLEGAME_API ADemoUnitActor : public AUnitActor
{
	GENERATED_BODY()

public:
	ADemoUnitActor();

	/** Demo faction (selection filter + capsule tint). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Demo")
	EDemoTeam Team = EDemoTeam::Player;

protected:
	// Editor-only placement aid: persistent debug lines showing the formation
	// footprint this actor will spawn (frontage = files x spacing across the
	// actor's right vector, depth behind, facing arrow forward). Refreshed on
	// every editor move/property change via OnConstruction.
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void DrawEditorFootprint() const;
};
