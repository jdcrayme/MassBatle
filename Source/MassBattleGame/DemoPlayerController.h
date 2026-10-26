// DemoPlayerController.h -- Total War-style selection & orders for the demo
// (interaction pattern adapted from the MassRTS RTSPlayerController). Input is
// programmatic Enhanced Input (actions + mapping context built in C++, no
// editor assets -- UE 5.8 deleted the legacy BindKey path). Pipeline: LMB
// click/marquee -> selection of player units (FUnitHandles via the visual
// subsystem); RMB click = move preserving relative offsets; RMB drag >
// threshold = formation gesture (the drag line is the FRONT line: start =
// left edge, end = right edge, facing = line normal; the line is divided
// across selected units and each gets a commanded file count from its segment
// width via the A2 width API); RMB on a garrison structure = garrison the
// selection into it (the plugin's FSM files the unit through the entrance).
// H = halt.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DemoTypes.h"
#include "DemoPlayerController.generated.h"

class ADemoCameraPawn;
class UDemoUnitVisualSubsystem;
class UInputAction;
class UInputMappingContext;
class UWorld;

UCLASS()
class MASSBATTLEGAME_API ADemoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADemoPlayerController();

	// -- APlayerController --
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	/** Garrison-order click resolution: the structure whose (margin-expanded)
	 *  footprint contains the world point (cm, planar), 0 when none. Shared by
	 *  the RMB order path, the Demo.GarrisonAt console command, and the game
	 *  mode's boot probe. */
	static uint32 FindStructureAtWorld(UWorld* World, const FVector2D& PointCm, float MarginCm = 300.f);

	// -- HUD probes (canvas HUD reads these) --
	bool IsMarqueeActive(FVector2D& OutStart, FVector2D& OutCurrent) const;
	bool IsOrderDragActive(FVector2D& OutStartScreen, FVector2D& OutCurrentScreen) const;
	/** Formation-drag layout recomputed for the current drag (preview + commit). */
	bool ComputeDragLayout(TArray<FDemoDragSegment>& OutSegments) const;

	const TArray<FUnitHandle>& GetSelectedUnits() const { return SelectedUnits; }

	// -- input handlers (bound to the programmatic actions) --
	void OnLMBPressed();
	void OnLMBReleased();
	void OnRMBPressed();
	void OnRMBReleased();
	void OnRotateLeftPressed() { HeldRotateKeys.Add(EKeys::Q); }
	void OnRotateLeftReleased() { HeldRotateKeys.Remove(EKeys::Q); }
	void OnRotateRightPressed() { HeldRotateKeys.Add(EKeys::E); }
	void OnRotateRightReleased() { HeldRotateKeys.Remove(EKeys::E); }
	void OnZoomIn();
	void OnZoomOut();
	void OnHalt();

private:
	/** Builds the demo's input actions + mapping context programmatically (no
	 *  editor assets; UE 5.8 removed the legacy BindKey path on
	 *  UEnhancedInputComponent). */
	void SetupEnhancedInput();
	void AddMapping(UInputMappingContext* IMC, UInputAction* Action, FKey Key) const;
	UInputAction* MakeBoolAction(const FName& Name, const FText& DisplayName);
	void SetPanDir(FVector2D Dir, bool bHeld);

	void UpdateCamera(float DeltaSeconds);
	bool PickUnitAtScreen(const FVector2D& ScreenPos, FUnitHandle& OutUnit) const;
	bool ScreenToGround(const FVector2D& ScreenPos, FVector2D& OutGroundCm) const;
	void SetSelection(const TArray<FUnitHandle>& NewSelection);
	/** Drops handles whose registry entry went stale. */
	void PruneStaleSelection();

	/** Click-vs-drag threshold (screen px). */
	static constexpr float ClickThresholdPx = 5.f;
	/** Formation-gesture threshold (screen px). Below it an RMB press is a
	 *  plain move (keeps facing); short deliberate drags still count. */
	static constexpr float FormationDragThresholdPx = 12.f;
	/** Pick radius around a projected unit center (screen px). */
	static constexpr float PickRadiusPx = 250.f;
	/** Lateral gap between unit segments when dividing a drag line (cm). */
	static constexpr float DragSegmentGapCm = 200.f;

	// -- selection state --
	TArray<FUnitHandle> SelectedUnits;
	bool bMarqueeActive = false;
	FVector2D MarqueeStartScreen = FVector2D::ZeroVector;

	// -- order drag state --
	bool bDraggingOrder = false;
	FVector2D DragStartScreen = FVector2D::ZeroVector;
	FVector2D DragStartWorldCm = FVector2D::ZeroVector;
	FVector2D DragCurrentWorldCm = FVector2D::ZeroVector;
	bool bDragIsFormation = false;

	// -- held input state --
	bool bPanFwd = false;
	bool bPanBack = false;
	bool bPanLeft = false;
	bool bPanRight = false;
	TSet<FKey> HeldRotateKeys; // Q = left, E = right

	/** Programmatic actions/IMC (kept referenced so bindings stay valid). */
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> OwnedActions;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DemoIMC;

	ADemoCameraPawn* CameraPawn() const;
	UDemoUnitVisualSubsystem* VisualSubsystem() const;
};
