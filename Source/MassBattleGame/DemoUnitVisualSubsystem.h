// DemoUnitVisualSubsystem.h -- soldier visualization for the demo (Phase 5
// territory pulled forward minimally). One UInstancedStaticMeshComponent per
// registered unit; each tick copies the roster's FTransformFragment transforms
// into the ISM. The capsule static mesh is generated at runtime (no capsule
// exists among the engine basic shapes). Also derives per-unit body info from
// the formation shared fragment for picking / HUD / drag math (same math as
// the soak controller's body-bounds probe).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoTypes.h"
#include "DemoUnitVisualSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;

UCLASS()
class MASSBATTLEGAME_API UDemoUnitVisualSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// -- USubsystem --
	virtual void Deinitialize() override;

	// -- FTickableGameObject (UTickableWorldSubsystem) --
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Called by the game mode after a level-placed unit actor spawns its
	 *  roster. Creates the unit's ISM lazily on the next tick. */
	void RegisterUnit(FUnitHandle Handle, EDemoTeam Team);

	/** Faction split for selection filtering (live handles only). */
	void GetUnitsOfTeam(EDemoTeam Team, TArray<FUnitHandle>& OutHandles) const;

	/** Live handles in stable (registration) order, any faction. */
	const TArray<FUnitHandle>& GetAllUnits() const { return Units; }

	EDemoTeam GetTeam(FUnitHandle Handle) const;

	/** Selection tint (driven by the player controller). */
	void SetSelected(FUnitHandle Handle, bool bSelected);

	bool IsSelected(FUnitHandle Handle) const;

	/** Body summary from the head entity's formation shared fragment. False
	 *  for stale handles / units without a spawned roster yet. */
	bool GetUnitBodyInfo(FUnitHandle Handle, FDemoUnitBodyInfo& OutInfo) const;

private:
	UInstancedStaticMeshComponent* EnsureVisual(FUnitHandle Handle, EDemoTeam Team);
	UStaticMesh* EnsureCapsuleMesh();
	void UpdateSelectionTint(FUnitHandle Handle);

	/** Registration order = stable draw/iterate order. */
	TArray<FUnitHandle> Units;
	TMap<FUnitHandle, EDemoTeam> TeamByUnit;
	TMap<FUnitHandle, TObjectPtr<UInstancedStaticMeshComponent>> VisualByUnit;
	TSet<FUnitHandle> Selected;
	TMap<EDemoTeam, FLinearColor> TeamColors;
	bool bSelectedDirty = false;

	/** Lazily built runtime capsule mesh (origin at the base, z = 0). */
	TObjectPtr<UStaticMesh> CapsuleMesh;
};
