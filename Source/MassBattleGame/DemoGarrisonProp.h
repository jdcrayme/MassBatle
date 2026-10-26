// DemoGarrisonProp.h -- level-placed garrison structure for the demo map.
// Registers a gray-box FGarrisonStructureRecord with the plugin's garrison
// subsystem at its transform (plugin public API only -- MakeFixture +
// RegisterStructure), stamps the cost grid so field A* routes approaches to
// the entrance, and draws a minimal gray-box body so the structure reads on
// screen. This is the editor-authored half of the phase-garrison fixture
// story: place it in the map like an ADemoUnitActor; the sim consumes only
// the plain record.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BattleGarrisonTypes.h"
#include "DemoGarrisonProp.generated.h"

class UStaticMeshComponent;

UCLASS(Blueprintable)
class MASSBATTLEGAME_API ADemoGarrisonProp : public AActor
{
	GENERATED_BODY()

public:
	ADemoGarrisonProp();

	// -- AActor --
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

protected:
	/** Structure family (fixture geometry comes from the plugin's
	 *  FGarrisonLayout::MakeFixture -- building 2 units / wall + trench 1).
	 *  Public setter for deferred-spawn bootstrap: the type must be assigned
	 *  before BeginPlay dispatches (registration happens there). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Demo")
	EGarrisonStructureType StructureType = EGarrisonStructureType::Building;

public:
	void SetStructureType(EGarrisonStructureType InType) { StructureType = InType; }

	/** Register the record + stamp the cost grid on BeginPlay. Off = preview
	 *  boxes only (placement authoring without touching the sim). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Demo")
	bool bRegisterOnBeginPlay = true;

	/** Editor-only placement aid: persistent footprint/entrance lines. */
	void DrawPlacementPreview();

	/** Gray-box body boxes (walls / berms), rebuilt in OnConstruction. */
	void RebuildBody();

	/** Assigned by the subsystem at registration (0 = not registered). */
	uint32 StructureId = 0;

	/** Gray-box body components (transient; rebuilt on construction). */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BodyComponents;
};
