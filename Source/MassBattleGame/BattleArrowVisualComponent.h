// BattleArrowVisualComponent.h -- PR-8 gray-box arrow renderer (M4). The
// projectile pool's SoA layout IS the render feed (spec ranged-stack section
// 10): one UInstancedStaticMeshComponent batched from
// UBattleProjectileSubsystem each frame, one draw call regardless of arrow
// count. This is the interim seam, NOT the destination -- Phase 5 owns the
// full representation bands (one Niagara batch per volley, arrow-stick
// decals). Demo-module only: consumes the plugin's public pool API (ADR-18
// pattern); the sim never knew about rendering.
#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "BattleArrowVisualComponent.generated.h"

class UStaticMesh;

UCLASS(ClassGroup = (BattleDemo), meta = (BlueprintSpawnableComponent))
class MASSBATTLEGAME_API UBattleArrowVisualComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UBattleArrowVisualComponent();

	// -- UActorComponent --
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisFunction) override;

	/** Gray-box arrow mesh: low-poly shaft + head pointing +X, unlit
	 *  material, ~20 cm (B.1's capsules-first gray-box rule). Soft default
	 *  /Game/MassBattle/Demo/SM_Arrow (authored via MCP, PR-8). */
	UPROPERTY(EditAnywhere, Category = "Battle|Demo")
	TSoftObjectPtr<UStaticMesh> ArrowMesh;

	/** Render cap -- the pool's reserved capacity is the real bound (RC9);
	 *  this is only the oldest-first skip safety net. */
	UPROPERTY(EditAnywhere, Category = "Battle|Demo", meta = (ClampMin = "1"))
	int32 MaxInstances = 4096;
};
