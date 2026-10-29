// DemoPlayerController.h -- Total War-style selection & orders for the demo
// (interaction pattern adapted from the MassRTS RTSPlayerController). Input is
// Enhanced Input with the mapping table in editor data (PR-2: IMC_Demo + the
// IA_Demo_* assets under /Game/MassBattle/Demo/Input, retunable without a
// recompile); the programmatic C++ construction is the one-release fallback
// for a missing asset. Pipeline: LMB
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

	/** Team of the selection's first unit (Player when the selection is
	 *  empty). The RMB attack path splits on this. */
	EDemoTeam GetSelectionTeam() const;

	// -- input handlers (bound to the programmatic actions) --
	void OnLMBPressed();
	void OnLMBReleased();

	/** Phase 2 task 10: attack orders (A4 surface on the demo input):
	 *  RMB on an enemy unit = AttackUnit for the whole selection;
	 *  Ctrl+RMB on ground = AttackMove to the point (auto-engage en route).
	 *  Returns true when the click was CONSUMED (an attack order was
	 *  issued) -- the caller must stop: the plain-move paths that follow
	 *  would otherwise overwrite the attack order with a Move to the same
	 *  point (the registry holds ONE current order per unit). */
	bool IssueAttackOrders(const FVector2D& ScreenPos, const FVector2D& GroundCm);

	/** The flat-tile destination clamp (shared by the move + attack-move
	 *  order paths). */
	FVector2D ClampToGridHelper(const FVector2D& P) const;
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
	/** The input mapping table (which key fires which action) is editor data:
	 *  IMC_Demo + the IA_Demo_* action assets (PR-2). Bindings stay in code --
	 *  they are behavior. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> DemoIMCAsset;

	/** The active context (loaded asset, or the fallback-built IMC). Pushed in
	 *  BeginPlay; kept referenced so bindings stay valid. */
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DemoIMC;

	/** The 11 actions the bindings consume, in one place regardless of whether
	 *  they came from the asset or the fallback construction. */
	struct FDemoActionSet
	{
		const UInputAction* Select = nullptr;
		const UInputAction* Order = nullptr;
		const UInputAction* Halt = nullptr;
		const UInputAction* ZoomIn = nullptr;
		const UInputAction* ZoomOut = nullptr;
		const UInputAction* PanFwd = nullptr;
		const UInputAction* PanBack = nullptr;
		const UInputAction* PanLeft = nullptr;
		const UInputAction* PanRight = nullptr;
		const UInputAction* RotLeft = nullptr;
		const UInputAction* RotRight = nullptr;
	};

	/** Loads IMC_Demo and resolves the 11 actions by name from it. False on
	 *  ANY missing piece (all-or-nothing: a partially bound controller is
	 *  worse than the fallback table). */
	bool TryLoadAssetInput(FDemoActionSet& OutActions);
	/** The pre-PR-2 programmatic construction (B-4), retained as the PR-2
	 *  one-release fallback; delete once PIE evidence is in. */
	void BuildProgrammaticInput(FDemoActionSet& OutActions);
	/** The bindings -- behavior, not data; shared by both input sources. */
	void BindDemoInput(const FDemoActionSet& A);

	void SetupEnhancedInput();
	void AddMapping(UInputMappingContext* IMC, const UInputAction* Action, FKey Key) const;
	UInputAction* MakeBoolAction(const FName& Name, const FText& DisplayName);
	void SetPanDir(FVector2D Dir, bool bHeld);

	void UpdateCamera(float DeltaSeconds);
	bool PickUnitAtScreen(const FVector2D& ScreenPos, FUnitHandle& OutUnit, EDemoTeam* TeamFilter = nullptr) const;
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
	/** F3 edge latch (toggles Battle.Debug.ShowOverlay). */
	bool bF3Latch = false;
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

	/** Fallback-built actions (kept referenced so bindings stay valid; the
	 *  asset path holds its actions through the IMC asset instead). */
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> OwnedActions;

	ADemoCameraPawn* CameraPawn() const;
	UDemoUnitVisualSubsystem* VisualSubsystem() const;
};
