// DemoPlayerController.cpp -- see header.
#include "DemoPlayerController.h"
#include "DemoCameraPawn.h"
#include "DemoGameMode.h"
#include "DemoUnitVisualSubsystem.h"

#include "BattleRegistry.h"
#include "BattleCostGrid.h"
#include "BattleGarrisonSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedActionKeyMapping.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

ADemoPlayerController::ADemoPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = false;
	DefaultMouseCursor = EMouseCursor::Crosshairs;
	// PR-2: the mapping table lives in editor data; this soft default is what
	// the authoring step fills (Content/MassBattle/Demo/Input/IMC_Demo).
	DemoIMCAsset = TSoftObjectPtr<UInputMappingContext>(
		FSoftObjectPath(TEXT("/Game/MassBattle/Demo/Input/IMC_Demo.IMC_Demo")));
}

void ADemoPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// The IMC + actions are built in SetupInputComponent (before BeginPlay);
	// pushing the context needs the local player, which is ready here.
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(DemoIMC, /*Priority*/ 0);
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoPC ready"));
}

// -- Enhanced Input: data (assets) + behavior (bindings, code) ------------------

bool ADemoPlayerController::TryLoadAssetInput(FDemoActionSet& OutActions)
{
	// Quiet existence probe first: the fallback is a DESIGNED operating mode
	// (CI/headless sessions run it forever), so a missing package must not
	// trip the engine's load-failure warning -- that would degrade every
	// automation test's report classification (measured 2026-10-29).
	const FSoftObjectPath Path = DemoIMCAsset.ToSoftObjectPath();
	if (!FPackageName::DoesPackageExist(Path.GetLongPackageName()))
	{
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoInput asset=%s not authored -- programmatic fallback in use (PR-2 rollback path)"),
			*Path.ToString());
		return false;
	}
	DemoIMC = DemoIMCAsset.LoadSynchronous();
	if (!DemoIMC)
	{
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoInput asset=%s unloadable -- programmatic fallback in use (PR-2 rollback path)"),
			*Path.ToString());
		return false;
	}

	const auto Resolve = [this](const TCHAR* Name) -> const UInputAction*
	{
		const FName Wanted(Name);
		for (const FEnhancedActionKeyMapping& M : DemoIMC->GetMappings())
		{
			if (M.Action && M.Action->GetFName() == Wanted)
			{
				return M.Action;
			}
		}
		return nullptr;
	};

	OutActions = FDemoActionSet{
		Resolve(TEXT("IA_Demo_Select")),  Resolve(TEXT("IA_Demo_Order")),
		Resolve(TEXT("IA_Demo_Halt")),    Resolve(TEXT("IA_Demo_ZoomIn")),
		Resolve(TEXT("IA_Demo_ZoomOut")), Resolve(TEXT("IA_Demo_PanFwd")),
		Resolve(TEXT("IA_Demo_PanBack")), Resolve(TEXT("IA_Demo_PanLeft")),
		Resolve(TEXT("IA_Demo_PanRight")), Resolve(TEXT("IA_Demo_RotLeft")),
		Resolve(TEXT("IA_Demo_RotRight")),
	};

	const UInputAction* Missing[] = { OutActions.Select, OutActions.Order, OutActions.Halt,
		OutActions.ZoomIn, OutActions.ZoomOut, OutActions.PanFwd, OutActions.PanBack,
		OutActions.PanLeft, OutActions.PanRight, OutActions.RotLeft, OutActions.RotRight };
	for (const UInputAction* A : Missing)
	{
		if (!A)
		{
			UE_LOG(LogDemoBattle, Warning,
				TEXT("EXEC_RESULT DemoInput IMC_Demo missing action(s) -- programmatic fallback in use (PR-2 rollback path)"));
			return false;
		}
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoInput IMC_Demo loaded actions=11"));
	return true;
}

UInputAction* ADemoPlayerController::MakeBoolAction(const FName& Name, const FText& DisplayName)
{
	UInputAction* IA = NewObject<UInputAction>(this, Name);
	IA->ValueType = EInputActionValueType::Boolean;
	IA->ActionDescription = DisplayName;
	OwnedActions.Add(IA);
	return IA;
}

void ADemoPlayerController::AddMapping(UInputMappingContext* IMC, const UInputAction* Action, FKey Key) const
{
	IMC->MapKey(Action, Key); // default implicit Down trigger suits bool actions
}

void ADemoPlayerController::BuildProgrammaticInput(FDemoActionSet& OutActions)
{
	OwnedActions.Reset();
	const auto Bool = [this](const TCHAR* Name, const TCHAR* Display) -> UInputAction*
	{
		return MakeBoolAction(Name, FText::FromString(Display));
	};

	OutActions.Select = Bool(TEXT("IA_DemoSelect"), TEXT("Select / marquee"));
	OutActions.Order = Bool(TEXT("IA_DemoOrder"), TEXT("Move / formation drag"));
	OutActions.Halt = Bool(TEXT("IA_DemoHalt"), TEXT("Halt selection"));
	OutActions.ZoomIn = Bool(TEXT("IA_DemoZoomIn"), TEXT("Zoom in"));
	OutActions.ZoomOut = Bool(TEXT("IA_DemoZoomOut"), TEXT("Zoom out"));
	OutActions.PanFwd = Bool(TEXT("IA_DemoPanFwd"), TEXT("Pan forward"));
	OutActions.PanBack = Bool(TEXT("IA_DemoPanBack"), TEXT("Pan back"));
	OutActions.PanLeft = Bool(TEXT("IA_DemoPanLeft"), TEXT("Pan left"));
	OutActions.PanRight = Bool(TEXT("IA_DemoPanRight"), TEXT("Pan right"));
	OutActions.RotLeft = Bool(TEXT("IA_DemoRotLeft"), TEXT("Rotate left"));
	OutActions.RotRight = Bool(TEXT("IA_DemoRotRight"), TEXT("Rotate right"));

	DemoIMC = NewObject<UInputMappingContext>(this, TEXT("IMC_Demo"));
	AddMapping(DemoIMC, OutActions.Select, EKeys::LeftMouseButton);
	AddMapping(DemoIMC, OutActions.Order, EKeys::RightMouseButton);
	AddMapping(DemoIMC, OutActions.Halt, EKeys::H);
	AddMapping(DemoIMC, OutActions.ZoomIn, EKeys::MouseScrollUp);
	AddMapping(DemoIMC, OutActions.ZoomOut, EKeys::MouseScrollDown);
	AddMapping(DemoIMC, OutActions.PanFwd, EKeys::W);
	AddMapping(DemoIMC, OutActions.PanFwd, EKeys::Up);
	AddMapping(DemoIMC, OutActions.PanBack, EKeys::S);
	AddMapping(DemoIMC, OutActions.PanBack, EKeys::Down);
	AddMapping(DemoIMC, OutActions.PanLeft, EKeys::A);
	AddMapping(DemoIMC, OutActions.PanLeft, EKeys::Left);
	AddMapping(DemoIMC, OutActions.PanRight, EKeys::D);
	AddMapping(DemoIMC, OutActions.PanRight, EKeys::Right);
	AddMapping(DemoIMC, OutActions.RotLeft, EKeys::Q);
	AddMapping(DemoIMC, OutActions.RotRight, EKeys::E);
}

void ADemoPlayerController::BindDemoInput(const FDemoActionSet& A)
{
	UEnhancedInputComponent* IC = CastChecked<UEnhancedInputComponent>(InputComponent);
	IC->BindAction(A.Select, ETriggerEvent::Started, this, &ADemoPlayerController::OnLMBPressed);
	IC->BindAction(A.Select, ETriggerEvent::Completed, this, &ADemoPlayerController::OnLMBReleased);
	IC->BindAction(A.Order, ETriggerEvent::Started, this, &ADemoPlayerController::OnRMBPressed);
	IC->BindAction(A.Order, ETriggerEvent::Completed, this, &ADemoPlayerController::OnRMBReleased);
	IC->BindAction(A.Halt, ETriggerEvent::Started, this, &ADemoPlayerController::OnHalt);
	IC->BindAction(A.ZoomIn, ETriggerEvent::Started, this, &ADemoPlayerController::OnZoomIn);
	IC->BindAction(A.ZoomOut, ETriggerEvent::Started, this, &ADemoPlayerController::OnZoomOut);
	IC->BindActionValueLambda(A.PanFwd, ETriggerEvent::Started, [this](const FInputActionValue&) { SetPanDir(FVector2D(1.f, 0.f), true); });
	IC->BindActionValueLambda(A.PanFwd, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetPanDir(FVector2D(1.f, 0.f), false); });
	IC->BindActionValueLambda(A.PanBack, ETriggerEvent::Started, [this](const FInputActionValue&) { SetPanDir(FVector2D(-1.f, 0.f), true); });
	IC->BindActionValueLambda(A.PanBack, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetPanDir(FVector2D(-1.f, 0.f), false); });
	IC->BindActionValueLambda(A.PanLeft, ETriggerEvent::Started, [this](const FInputActionValue&) { SetPanDir(FVector2D(0.f, -1.f), true); });
	IC->BindActionValueLambda(A.PanLeft, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetPanDir(FVector2D(0.f, -1.f), false); });
	IC->BindActionValueLambda(A.PanRight, ETriggerEvent::Started, [this](const FInputActionValue&) { SetPanDir(FVector2D(0.f, 1.f), true); });
	IC->BindActionValueLambda(A.PanRight, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetPanDir(FVector2D(0.f, 1.f), false); });
	IC->BindAction(A.RotLeft, ETriggerEvent::Started, this, &ADemoPlayerController::OnRotateLeftPressed);
	IC->BindAction(A.RotLeft, ETriggerEvent::Completed, this, &ADemoPlayerController::OnRotateLeftReleased);
	IC->BindAction(A.RotRight, ETriggerEvent::Started, this, &ADemoPlayerController::OnRotateRightPressed);
	IC->BindAction(A.RotRight, ETriggerEvent::Completed, this, &ADemoPlayerController::OnRotateRightReleased);
}

void ADemoPlayerController::SetupEnhancedInput()
{
	FDemoActionSet Actions;
	if (!TryLoadAssetInput(Actions))
	{
		BuildProgrammaticInput(Actions);
	}
	BindDemoInput(Actions);
}

void ADemoPlayerController::SetPanDir(FVector2D Dir, bool bHeld)
{
	// Direction-keyed flags: both keys of a direction (W + Up) fold into one
	// logical entry; a repeated press of the second key cannot "cancel" the
	// first because each direction action only drives its own flag.
	if (Dir.X > 0.f) { bPanFwd = bHeld; }
	else if (Dir.X < 0.f) { bPanBack = bHeld; }
	else if (Dir.Y < 0.f) { bPanLeft = bHeld; }
	else if (Dir.Y > 0.f) { bPanRight = bHeld; }
}

void ADemoPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	SetupEnhancedInput();
}

ADemoCameraPawn* ADemoPlayerController::CameraPawn() const
{
	return Cast<ADemoCameraPawn>(GetPawn());
}

UDemoUnitVisualSubsystem* ADemoPlayerController::VisualSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
}

// -- garrison click resolution -------------------------------------------------

uint32 ADemoPlayerController::FindStructureAtWorld(UWorld* World, const FVector2D& PointCm, float MarginCm)
{
	UBattleGarrisonSubsystem* Garrison = World ? World->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr;
	if (!Garrison)
	{
		return 0;
	}
	const FVector2D PointM = PointCm * 0.01f; // fragment boundary: click point cm -> record meters
	const float MarginM = MarginCm * 0.01f;
	for (const TPair<uint32, FGarrisonStructureRecord>& It : Garrison->GetAllStructures())
	{
		// The margin covers thin bodies (the 4 m wall/trench channels): a
		// cursor click on a wall face lands near, not exactly on, the record
		// footprint -- and the no-collision gray-box bodies let the cursor
		// trace pass through to the ground plane behind them.
		if (It.Value.Footprint.ExpandBy(MarginM).IsInside(PointM))
		{
			return It.Key;
		}
	}
	return 0;
}

// -- per-frame ---------------------------------------------------------------

void ADemoPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateCamera(DeltaSeconds);
	PruneStaleSelection();

	// F3: toggle the battle debug overlay (slot/attack/corpse graphics).
	{
		const bool bF3 = IsInputKeyDown(EKeys::F3);
		if (bF3 && !bF3Latch)
		{
			if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Debug.ShowOverlay")))
			{
				const int32 New = C->GetInt() != 0 ? 0 : 1;
				C->Set(New, ECVF_SetByConsole);
				UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoDebug overlay=%d (F3)"), New);
			}
		}
		bF3Latch = bF3;
	}

	// Order-drag tracking: keep the world-space cursor current; flag the
	// formation gesture once the screen drag clears the threshold.
	if (bDraggingOrder)
	{
		float MouseX, MouseY;
		if (GetMousePosition(MouseX, MouseY))
		{
			ScreenToGround(FVector2D(MouseX, MouseY), DragCurrentWorldCm);
			if (!bDragIsFormation)
			{
				const FVector2D CurrentScreen(MouseX, MouseY);
				bDragIsFormation = (CurrentScreen - DragStartScreen).Size() > FormationDragThresholdPx;
			}
		}
	}
}

void ADemoPlayerController::UpdateCamera(float DeltaSeconds)
{
	ADemoCameraPawn* Cam = CameraPawn();
	if (!Cam)
	{
		return;
	}

	// Held-key pan (screen directions, rotated by the camera rig).
	FVector2D Pan(0.f, 0.f);
	Pan.X += (bPanFwd ? 1.f : 0.f) - (bPanBack ? 1.f : 0.f);
	Pan.Y += (bPanRight ? 1.f : 0.f) - (bPanLeft ? 1.f : 0.f);

	// Edge scroll: ramp inside a border zone, dead zone against jitter.
	float MouseX, MouseY;
	if (GetMousePosition(MouseX, MouseY) && GEngine && GEngine->GameViewport)
	{
		FVector2D ViewSize;
		GEngine->GameViewport->GetViewportSize(ViewSize);
		const float ZoneFraction = Cam->GetEdgeScrollZone();
		const float DeadPx = Cam->GetEdgeDeadZonePx();
		auto Edge = [MouseX, MouseY, ViewSize, ZoneFraction, DeadPx](float Pos, float Size, bool bFromMin) -> float
		{
			const float Zone = Size * ZoneFraction;
			const float FromEdge = bFromMin ? Pos : Size - Pos;
			if (FromEdge > Zone)
			{
				return 0.f;
			}
			return FMath::Clamp((Zone - FromEdge) / FMath::Max(Zone - DeadPx, 1.f), 0.f, 1.f);
		};
		const float Left = Edge(MouseX, ViewSize.X, true);
		const float Right = Edge(MouseX, ViewSize.X, false);
		const float Top = Edge(MouseY, ViewSize.Y, true);
		const float Bottom = Edge(MouseY, ViewSize.Y, false);
		Pan.Y += (Right - Left);
		Pan.X += (Top - Bottom);
	}

	if (Cam->GetWorld())
	{
		// Screen dirs as rig-relative (forward, right); the pawn applies its yaw.
		Cam->Pan(Pan);
	}

	float Rotate = 0.f;
	if (HeldRotateKeys.Contains(EKeys::Q))
	{
		Rotate -= 1.f;
	}
	if (HeldRotateKeys.Contains(EKeys::E))
	{
		Rotate += 1.f;
	}
	Cam->Rotate(Rotate);
}

// -- selection ---------------------------------------------------------------

void ADemoPlayerController::PruneStaleSelection()
{
	if (UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr)
	{
		const bool bHadStale = SelectedUnits.ContainsByPredicate(
			[Registry](const FUnitHandle& H) { return !Registry->IsHandleValid(H); });
		if (bHadStale)
		{
			TArray<FUnitHandle> Kept;
			for (const FUnitHandle& H : SelectedUnits)
			{
				if (Registry->IsHandleValid(H))
				{
					Kept.Add(H);
				}
			}
			SetSelection(Kept);
		}
	}
}

void ADemoPlayerController::SetSelection(const TArray<FUnitHandle>& NewSelection)
{
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (Visuals)
	{
		for (const FUnitHandle& H : SelectedUnits)
		{
			Visuals->SetSelected(H, false);
		}
		for (const FUnitHandle& H : NewSelection)
		{
			Visuals->SetSelected(H, true);
		}
	}
	SelectedUnits = NewSelection;
}

bool ADemoPlayerController::ScreenToGround(const FVector2D& ScreenPos, FVector2D& OutGroundCm) const
{
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex*/ false, Hit) && Hit.bBlockingHit)
	{
		OutGroundCm = FVector2D(Hit.ImpactPoint.X, Hit.ImpactPoint.Y);
		return true;
	}
	// Fallback: deproject and intersect the z=0 plane (ground without a
	// visibility channel still yields usable orders).
	FVector WorldOrigin, WorldDir;
	if (DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, WorldOrigin, WorldDir)
		&& !FMath::IsNearlyZero(WorldDir.Z))
	{
		const float T = -WorldOrigin.Z / WorldDir.Z;
		if (T > 0.f)
		{
			OutGroundCm = FVector2D(WorldOrigin.X + WorldDir.X * T, WorldOrigin.Y + WorldDir.Y * T);
			return true;
		}
	}
	return false;
}

EDemoTeam ADemoPlayerController::GetSelectionTeam() const
{
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (Visuals && SelectedUnits.Num() > 0)
	{
		return Visuals->GetTeam(SelectedUnits[0]);
	}
	return EDemoTeam::Player;
}

bool ADemoPlayerController::PickUnitAtScreen(const FVector2D& ScreenPos, FUnitHandle& OutUnit, EDemoTeam* TeamFilter) const
{
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (!Visuals)
	{
		return false;
	}
	float BestSq = FMath::Square(PickRadiusPx);
	bool bFound = false;
	for (const FUnitHandle& Handle : Visuals->GetAllUnits())
	{
		if (TeamFilter && Visuals->GetTeam(Handle) != *TeamFilter)
		{
			continue;
		}
		FDemoUnitBodyInfo Info;
		if (!Visuals->GetUnitBodyInfo(Handle, Info))
		{
			continue;
		}
		FVector2D Proj;
		if (!ProjectWorldLocationToScreen(FVector(Info.BodyCenterCm.X, Info.BodyCenterCm.Y, DemoUi::BodyLabelZCm), Proj))
		{
			continue;
		}
		const float DistSq = FVector2D::DistSquared(Proj, ScreenPos);
		if (DistSq < BestSq)
		{
			BestSq = DistSq;
			OutUnit = Handle;
			bFound = true;
		}
	}
	return bFound;
}

void ADemoPlayerController::OnLMBPressed()
{
	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}
	bMarqueeActive = true;
	MarqueeStartScreen = FVector2D(MouseX, MouseY);
}

void ADemoPlayerController::OnLMBReleased()
{
	if (!bMarqueeActive)
	{
		return;
	}
	bMarqueeActive = false;

	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (!Visuals)
	{
		return;
	}
	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}
	const FVector2D Current(MouseX, MouseY);
	const bool bClick = (Current - MarqueeStartScreen).SizeSquared() < FMath::Square(ClickThresholdPx);

	// Shift accumulates selection (TW standard).
	TArray<FUnitHandle> NewSelection = SelectedUnits;

	if (bClick)
	{
		FUnitHandle Picked;
		EDemoTeam PlayerTeam = EDemoTeam::Player;
		if (PickUnitAtScreen(Current, Picked, &PlayerTeam))
		{
			const int32 Existing = NewSelection.Find(Picked);
			if (IsInputKeyDown(EKeys::LeftShift))
			{
				if (Existing != INDEX_NONE)
				{
					NewSelection.RemoveAt(Existing);
				}
				else
				{
					NewSelection.Add(Picked);
				}
			}
			else
			{
				NewSelection.Reset();
				NewSelection.Add(Picked);
			}
		}
		else if (!IsInputKeyDown(EKeys::LeftShift))
		{
			NewSelection.Reset(); // empty click clears
		}
	}
	else
	{
		// Marquee: unit selected when its projected body center is in the box.
		const FBox2D Box(FVector2D::Min(MarqueeStartScreen, Current), FVector2D::Max(MarqueeStartScreen, Current));
		TArray<FUnitHandle> Marquee;
		TArray<FUnitHandle> PlayerUnits;
		Visuals->GetUnitsOfTeam(EDemoTeam::Player, PlayerUnits);
		for (const FUnitHandle& Handle : PlayerUnits)
		{
			FDemoUnitBodyInfo Info;
			if (!Visuals->GetUnitBodyInfo(Handle, Info))
			{
				continue;
			}
			FVector2D Proj;
			if (ProjectWorldLocationToScreen(FVector(Info.BodyCenterCm.X, Info.BodyCenterCm.Y, DemoUi::BodyLabelZCm), Proj)
				&& Box.IsInside(Proj))
			{
				Marquee.Add(Handle);
			}
		}
		if (IsInputKeyDown(EKeys::LeftShift))
		{
			for (const FUnitHandle& H : Marquee)
			{
				NewSelection.AddUnique(H);
			}
		}
		else
		{
			NewSelection = Marquee;
		}
	}

	SetSelection(NewSelection);
}

// -- orders ------------------------------------------------------------------

bool ADemoPlayerController::IsMarqueeActive(FVector2D& OutStart, FVector2D& OutCurrent) const
{
	if (!bMarqueeActive)
	{
		return false;
	}
	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	OutStart = MarqueeStartScreen;
	OutCurrent = FVector2D(MouseX, MouseY);
	return true;
}

bool ADemoPlayerController::IsOrderDragActive(FVector2D& OutStartScreen, FVector2D& OutCurrentScreen) const
{
	if (!bDraggingOrder)
	{
		return false;
	}
	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	OutStartScreen = DragStartScreen;
	OutCurrentScreen = FVector2D(MouseX, MouseY);
	return true;
}

void ADemoPlayerController::OnRMBPressed()
{
	if (SelectedUnits.Num() == 0)
	{
		return;
	}
	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}
	bDraggingOrder = true;
	bDragIsFormation = false;
	DragStartScreen = FVector2D(MouseX, MouseY);
	if (!ScreenToGround(DragStartScreen, DragStartWorldCm))
	{
		bDraggingOrder = false;
		return;
	}
	DragCurrentWorldCm = DragStartWorldCm;
}

void ADemoPlayerController::OnRMBReleased()
{
	if (!bDraggingOrder)
	{
		return;
	}
	bDraggingOrder = false;
	if (SelectedUnits.Num() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (!Registry || !Visuals)
	{
		return;
	}
	float MouseX, MouseY;
	const bool bHasMouse = GetMousePosition(MouseX, MouseY);

	// Right-click on a garrisonable structure garrisons the selection (ETW
	// semantics): the structure footprint is stamped impassable, so a move
	// order onto it plans as unreachable -- intercept before move handling.
	// A drag ENDING on a structure also garrisons (the endpoint is the intent;
	// drag elsewhere deploys normally). The plugin's FSM files the unit
	// through the entrance; re-garrisoning the same structure is a no-op.
	if (const uint32 StructureId = FindStructureAtWorld(World, DragCurrentWorldCm); StructureId != 0)
	{
		int32 Issued = 0;
		for (const FUnitHandle& H : SelectedUnits)
		{
			if (Registry->IssueGarrisonOrder(H, StructureId))
			{
				++Issued;
			}
		}
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoOrder Garrison Issued=%d of %d Structure=%u At=(%.1f,%.1f)m"),
			Issued, SelectedUnits.Num(), StructureId,
			DragCurrentWorldCm.X * 0.01f, DragCurrentWorldCm.Y * 0.01f);
		return;
	}

	// Attack orders (phase 2 task 10, A4 on the demo input): RMB on an ENEMY
	// unit = AttackUnit for the whole selection; Ctrl+RMB on ground =
	// AttackMove to the point (march + auto-engage en route). The garrison
	// intercept above keeps ETW structure semantics. A CONSUMED click (any
	// attack order issued) must STOP the handler here: the move paths below
	// call IssueMoveOrder on the same click point, and the registry holds ONE
	// current order per unit -- the Move would REPLACE the attack order, the
	// unit would march but never want combat, no engagement records would
	// form, and the assign pass would run its disengage branch every tick
	// (measured 2026-10-28: 'AttackUnit Issued=1 of 1' then 'DemoOrder Move
	// Units=1' on one click -- the attacker walked to the defender, squished
	// at the Block policy, and nothing fought).
	if (bHasMouse && IssueAttackOrders(FVector2D(MouseX, MouseY), DragCurrentWorldCm))
	{
		return;
	}

	if (bDragIsFormation)
	{
		// Formation gesture: divide the drag line across selected units; each
		// segment is that unit's front line (anchor = front-rank center on the
		// line) with facing = line normal. File counts come from segment width
		// via the A2 width API.
		TArray<FDemoDragSegment> Segments;
		if (ComputeDragLayout(Segments) && Segments.Num() > 0)
		{
			for (const FDemoDragSegment& Seg : Segments)
			{
				Registry->IssueMoveOrder(Seg.Handle, ClampToGridHelper(Seg.AnchorCm), Seg.FacingYawDeg,
					/*speed m/s*/ 0.f, FMath::Max(0, Seg.Files));
			}
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoOrder FormationDrag Units=%d"), Segments.Num());
			return;
		}
		// Degenerate line (zero length): fall through to a plain move.
	}

	// Plain click-move: preserve the selection's relative offsets around the
	// clicked point; face the direction of travel.
	FDemoUnitBodyInfo FirstInfo;
	FVector2D Centroid = FVector2D::ZeroVector;
	int32 Counted = 0;
	for (const FUnitHandle& H : SelectedUnits)
	{
		FDemoUnitBodyInfo Info;
		if (Visuals->GetUnitBodyInfo(H, Info))
		{
			Centroid += Info.BodyCenterCm;
			++Counted;
		}
	}
	if (Counted == 0)
	{
		return;
	}
	Centroid /= static_cast<float>(Counted);
	const FVector2D Dest = ClampToGridHelper(DragCurrentWorldCm);

	for (const FUnitHandle& H : SelectedUnits)
	{
		FDemoUnitBodyInfo Info;
		if (!Visuals->GetUnitBodyInfo(H, Info))
		{
			continue;
		}
		const FVector2D UnitDest = Dest + (Info.BodyCenterCm - Centroid);
		// TW click-move semantics: a plain click moves without re-facing -- the
		// unit keeps its current front (only an explicit drag commands a facing).
		Registry->IssueMoveOrder(H, ClampToGridHelper(UnitDest), Info.FacingYawDeg);
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoOrder Move Units=%d"), SelectedUnits.Num());
}

bool ADemoPlayerController::IssueAttackOrders(const FVector2D& ScreenPos, const FVector2D& GroundCm)
{
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Visuals || !Registry || SelectedUnits.Num() == 0)
	{
		return false;
	}
	const EDemoTeam MyTeam = GetSelectionTeam();

	// RMB on an ENEMY unit: the whole selection AttackUnits it (the sim's
	// assign pass fans the pairs out at ring distance).
	FUnitHandle Enemy;
	EDemoTeam EnemyTeam = EDemoTeam::Enemy;
	if (PickUnitAtScreen(ScreenPos, Enemy, &EnemyTeam) && EnemyTeam != MyTeam)
	{
		int32 Issued = 0;
		for (const FUnitHandle& H : SelectedUnits)
		{
			if (H != Enemy && Registry->IssueAttackUnitOrder(H, Enemy))
			{
				++Issued;
			}
		}
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoOrder AttackUnit Issued=%d of %d Target=%u:%u"),
			Issued, SelectedUnits.Num(), Enemy.Index, Enemy.Serial);
		return true;
	}

	// Ctrl+RMB on ground: AttackMove to the point (march + auto-engage en
	// route -- the spec's A4 disengage-by-retask semantics included).
	if (IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl))
	{
		const FVector2D Dest = ClampToGridHelper(GroundCm);
		int32 Issued = 0;
		for (const FUnitHandle& H : SelectedUnits)
		{
			FDemoUnitBodyInfo Info;
			const float Yaw = Visuals->GetUnitBodyInfo(H, Info) ? Info.FacingYawDeg : 0.f;
			if (Registry->IssueAttackMoveOrder(H, Dest, Yaw))
			{
				++Issued;
			}
		}
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoOrder AttackMove Issued=%d of %d At=(%.1f,%.1f)m"),
			Issued, SelectedUnits.Num(), Dest.X * 0.01f, Dest.Y * 0.01f);
		return true;
	}

	// Plain RMB (no enemy, no Ctrl): fall through to the move/formation
	// handling in OnRMBReleased.
	return false;
}

FVector2D ADemoPlayerController::ClampToGridHelper(const FVector2D& P) const
{
	// The movement tile extent is the plugin's grid geometry -- read from its
	// one home (BattleGridGeo, PR-3), never re-typed here. The 3 m margin off
	// the half-tile is demo-owned UX (orders keep off the impassable edge).
	constexpr float EdgeMarginCm = 300.f;
	const float Limit = BattleGridGeo::TileHalfExtentM * 100.f - EdgeMarginCm;
	return FVector2D(FMath::Clamp(P.X, -Limit, Limit), FMath::Clamp(P.Y, -Limit, Limit));
}

bool ADemoPlayerController::ComputeDragLayout(TArray<FDemoDragSegment>& OutSegments) const
{
	OutSegments.Reset();
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Visuals || !Registry || SelectedUnits.Num() == 0)
	{
		return false;
	}

	const FVector2D Line = DragCurrentWorldCm - DragStartWorldCm;
	const float LineLen = Line.Size();
	if (LineLen < KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const FVector2D Dir = Line / LineLen;
	// TW convention: the drag line's normal is the facing; reversing the draw
	// direction flips it (start = left edge, end = right edge).
	const FVector2D Normal(Dir.Y, -Dir.X);
	const float FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(Normal.Y, Normal.X));

	// Order the selected units along the drag direction by their current
	// positions so units do not cross each other.
	struct FOrderedUnit { FUnitHandle Handle; FVector2D BodyCm; int32 NumRanks; float FileSpacingM; int32 MaxFiles; float Proj; };
	TArray<FOrderedUnit> Ordered;
	for (const FUnitHandle& H : SelectedUnits)
	{
		FDemoUnitBodyInfo Info;
		if (!Visuals->GetUnitBodyInfo(H, Info))
		{
			continue;
		}
		FOrderedUnit U;
		U.Handle = H;
		U.BodyCm = Info.BodyCenterCm;
		U.NumRanks = Info.NumRanks;
		U.FileSpacingM = Info.FileSpacingM;
		U.MaxFiles = FMath::Max(1, Info.SoldierCount);
		U.Proj = FVector2D::DotProduct(Info.BodyCenterCm, Dir);
		Ordered.Add(U);
	}
	Ordered.Sort([](const FOrderedUnit& A, const FOrderedUnit& B) { return A.Proj < B.Proj; });
	if (Ordered.Num() == 0)
	{
		return false;
	}

	// Divide the line: equal segments with a fixed gap between units.
	const float Gap = DragSegmentGapCm;
	const float SegW = FMath::Max(200.f, (LineLen - Gap * (Ordered.Num() - 1)) / Ordered.Num());
	for (int32 i = 0; i < Ordered.Num(); ++i)
	{
		const FOrderedUnit& U = Ordered[i];
		const FVector2D SegCenter = DragStartWorldCm + Dir * (i * (SegW + Gap) + SegW * 0.5f);
		FDemoDragSegment Seg;
		Seg.Handle = U.Handle;
		Seg.AnchorCm = SegCenter; // the line IS the front line (front-rank center)
		Seg.FacingYawDeg = FacingYaw;
		// Width from segment width (meters / file spacing), clamped to the
		// unit's roster size; resolved by the plugin's width control.
		Seg.Files = FMath::Clamp(FMath::RoundToInt((SegW * 0.01f) / FMath::Max(U.FileSpacingM, 0.1f)), 1, U.MaxFiles);
		Seg.NumRanks = FMath::Max(1, FMath::CeilToInt(static_cast<float>(U.MaxFiles) / Seg.Files));
		OutSegments.Add(Seg);
	}
	return true;
}

void ADemoPlayerController::OnHalt()
{
	if (UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr)
	{
		for (const FUnitHandle& H : SelectedUnits)
		{
			Registry->IssueStopOrder(H);
		}
		if (SelectedUnits.Num() > 0)
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoOrder Halt Units=%d"), SelectedUnits.Num());
		}
	}
}

// -- zoom ----------------------------------------------------------------------

void ADemoPlayerController::OnZoomIn()
{
	if (ADemoCameraPawn* Cam = CameraPawn())
	{
		Cam->Zoom(+1.f);
	}
}

void ADemoPlayerController::OnZoomOut()
{
	if (ADemoCameraPawn* Cam = CameraPawn())
	{
		Cam->Zoom(-1.f);
	}
}

// -- Demo.GarrisonAt console command -------------------------------------------
// Resolves the garrisonable structure at a world point through the SAME
// footprint test the RMB click path uses, then issues garrison orders -- the
// interactive/log-based evidence channel for click-to-garrison (and usable
// from the PIE console). No unit indices = all player-team units.
static FAutoConsoleCommandWithWorldAndArgs GDemoGarrisonAt(
	TEXT("Demo.GarrisonAt"),
	TEXT("<x_m> <y_m> [unitIndex...] -- garrison the structure under the world point (RMB-click semantics); no unit indices = all player units; EXEC_RESULT DemoGarrisonAt"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() < 2)
		{
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoGarrisonAt FAIL BadArgs (need: x_m y_m [unitIndex...])"));
			return;
		}
		const FVector2D PointCm(FCString::Atof(*Args[0]) * 100.f, FCString::Atof(*Args[1]) * 100.f);
		const uint32 StructureId = ADemoPlayerController::FindStructureAtWorld(World, PointCm);
		if (StructureId == 0)
		{
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoGarrisonAt MISS Point=(%.1f,%.1f)m (no structure footprint there)"),
				PointCm.X * 0.01f, PointCm.Y * 0.01f);
			return;
		}

		// Target units: explicit registry indices, else the whole player team.
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UDemoUnitVisualSubsystem* Visuals = World->GetSubsystem<UDemoUnitVisualSubsystem>();
		if (!Registry || !Visuals)
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGarrisonAt FAIL no subsystems"));
			return;
		}
		TArray<FUnitHandle> Targets;
		if (Args.Num() > 2)
		{
			for (const FUnitHandle& Handle : Registry->GetSortedHandles())
			{
				for (int32 i = 2; i < Args.Num(); ++i)
				{
					if (Handle.Index == static_cast<uint32>(FCString::Atoi(*Args[i])))
					{
						Targets.Add(Handle);
					}
				}
			}
		}
		else
		{
			Visuals->GetUnitsOfTeam(EDemoTeam::Player, Targets);
		}

		int32 Issued = 0;
		for (const FUnitHandle& Handle : Targets)
		{
			if (Registry->IssueGarrisonOrder(Handle, StructureId))
			{
				++Issued;
			}
		}
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoGarrisonAt OK Structure=%u Issued=%d of %d Point=(%.1f,%.1f)m"),
			StructureId, Issued, Targets.Num(), PointCm.X * 0.01f, PointCm.Y * 0.01f);
	}));
