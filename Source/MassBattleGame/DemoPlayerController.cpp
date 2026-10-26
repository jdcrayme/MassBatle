// DemoPlayerController.cpp -- see header.
#include "DemoPlayerController.h"
#include "DemoCameraPawn.h"
#include "DemoGameMode.h"
#include "DemoUnitVisualSubsystem.h"

#include "BattleRegistry.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"

ADemoPlayerController::ADemoPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = false;
	DefaultMouseCursor = EMouseCursor::Crosshairs;
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

// -- programmatic Enhanced Input (no editor assets) ------------------------------

UInputAction* ADemoPlayerController::MakeBoolAction(const FName& Name, const FText& DisplayName)
{
	UInputAction* IA = NewObject<UInputAction>(this, Name);
	IA->ValueType = EInputActionValueType::Boolean;
	IA->ActionDescription = DisplayName;
	OwnedActions.Add(IA);
	return IA;
}

void ADemoPlayerController::AddMapping(UInputMappingContext* IMC, UInputAction* Action, FKey Key) const
{
	IMC->MapKey(Action, Key); // default implicit Down trigger suits bool actions
}

void ADemoPlayerController::SetupEnhancedInput()
{
	OwnedActions.Reset();
	const auto Bool = [this](const TCHAR* Name, const TCHAR* Display) -> UInputAction*
	{
		return MakeBoolAction(Name, FText::FromString(Display));
	};

	UInputAction* IA_Select = Bool(TEXT("IA_DemoSelect"), TEXT("Select / marquee"));
	UInputAction* IA_Order = Bool(TEXT("IA_DemoOrder"), TEXT("Move / formation drag"));
	UInputAction* IA_Halt = Bool(TEXT("IA_DemoHalt"), TEXT("Halt selection"));
	UInputAction* IA_ZoomIn = Bool(TEXT("IA_DemoZoomIn"), TEXT("Zoom in"));
	UInputAction* IA_ZoomOut = Bool(TEXT("IA_DemoZoomOut"), TEXT("Zoom out"));
	UInputAction* IA_PanFwd = Bool(TEXT("IA_DemoPanFwd"), TEXT("Pan forward"));
	UInputAction* IA_PanBack = Bool(TEXT("IA_DemoPanBack"), TEXT("Pan back"));
	UInputAction* IA_PanLeft = Bool(TEXT("IA_DemoPanLeft"), TEXT("Pan left"));
	UInputAction* IA_PanRight = Bool(TEXT("IA_DemoPanRight"), TEXT("Pan right"));
	UInputAction* IA_RotLeft = Bool(TEXT("IA_DemoRotLeft"), TEXT("Rotate left"));
	UInputAction* IA_RotRight = Bool(TEXT("IA_DemoRotRight"), TEXT("Rotate right"));

	DemoIMC = NewObject<UInputMappingContext>(this, TEXT("IMC_Demo"));
	AddMapping(DemoIMC, IA_Select, EKeys::LeftMouseButton);
	AddMapping(DemoIMC, IA_Order, EKeys::RightMouseButton);
	AddMapping(DemoIMC, IA_Halt, EKeys::H);
	AddMapping(DemoIMC, IA_ZoomIn, EKeys::MouseScrollUp);
	AddMapping(DemoIMC, IA_ZoomOut, EKeys::MouseScrollDown);
	AddMapping(DemoIMC, IA_PanFwd, EKeys::W);
	AddMapping(DemoIMC, IA_PanFwd, EKeys::Up);
	AddMapping(DemoIMC, IA_PanBack, EKeys::S);
	AddMapping(DemoIMC, IA_PanBack, EKeys::Down);
	AddMapping(DemoIMC, IA_PanLeft, EKeys::A);
	AddMapping(DemoIMC, IA_PanLeft, EKeys::Left);
	AddMapping(DemoIMC, IA_PanRight, EKeys::D);
	AddMapping(DemoIMC, IA_PanRight, EKeys::Right);
	AddMapping(DemoIMC, IA_RotLeft, EKeys::Q);
	AddMapping(DemoIMC, IA_RotRight, EKeys::E);

	UEnhancedInputComponent* IC = CastChecked<UEnhancedInputComponent>(InputComponent);
	IC->BindAction(IA_Select, ETriggerEvent::Started, this, &ADemoPlayerController::OnLMBPressed);
	IC->BindAction(IA_Select, ETriggerEvent::Completed, this, &ADemoPlayerController::OnLMBReleased);
	IC->BindAction(IA_Order, ETriggerEvent::Started, this, &ADemoPlayerController::OnRMBPressed);
	IC->BindAction(IA_Order, ETriggerEvent::Completed, this, &ADemoPlayerController::OnRMBReleased);
	IC->BindAction(IA_Halt, ETriggerEvent::Started, this, &ADemoPlayerController::OnHalt);
	IC->BindAction(IA_ZoomIn, ETriggerEvent::Started, this, &ADemoPlayerController::OnZoomIn);
	IC->BindAction(IA_ZoomOut, ETriggerEvent::Started, this, &ADemoPlayerController::OnZoomOut);
	IC->BindActionValueLambda(IA_PanFwd, ETriggerEvent::Started, [this](const FInputActionValue&) { SetPanDir(FVector2D(1.f, 0.f), true); });
	IC->BindActionValueLambda(IA_PanFwd, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetPanDir(FVector2D(1.f, 0.f), false); });
	IC->BindActionValueLambda(IA_PanBack, ETriggerEvent::Started, [this](const FInputActionValue&) { SetPanDir(FVector2D(-1.f, 0.f), true); });
	IC->BindActionValueLambda(IA_PanBack, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetPanDir(FVector2D(-1.f, 0.f), false); });
	IC->BindActionValueLambda(IA_PanLeft, ETriggerEvent::Started, [this](const FInputActionValue&) { SetPanDir(FVector2D(0.f, -1.f), true); });
	IC->BindActionValueLambda(IA_PanLeft, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetPanDir(FVector2D(0.f, -1.f), false); });
	IC->BindActionValueLambda(IA_PanRight, ETriggerEvent::Started, [this](const FInputActionValue&) { SetPanDir(FVector2D(0.f, 1.f), true); });
	IC->BindActionValueLambda(IA_PanRight, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetPanDir(FVector2D(0.f, 1.f), false); });
	IC->BindAction(IA_RotLeft, ETriggerEvent::Started, this, &ADemoPlayerController::OnRotateLeftPressed);
	IC->BindAction(IA_RotLeft, ETriggerEvent::Completed, this, &ADemoPlayerController::OnRotateLeftReleased);
	IC->BindAction(IA_RotRight, ETriggerEvent::Started, this, &ADemoPlayerController::OnRotateRightPressed);
	IC->BindAction(IA_RotRight, ETriggerEvent::Completed, this, &ADemoPlayerController::OnRotateRightReleased);
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

// -- per-frame ---------------------------------------------------------------

void ADemoPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateCamera(DeltaSeconds);
	PruneStaleSelection();

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

bool ADemoPlayerController::PickUnitAtScreen(const FVector2D& ScreenPos, FUnitHandle& OutUnit) const
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
		if (Visuals->GetTeam(Handle) != EDemoTeam::Player)
		{
			continue;
		}
		FDemoUnitBodyInfo Info;
		if (!Visuals->GetUnitBodyInfo(Handle, Info))
		{
			continue;
		}
		FVector2D Proj;
		if (!ProjectWorldLocationToScreen(FVector(Info.BodyCenterCm.X, Info.BodyCenterCm.Y, 100.f), Proj))
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
		if (PickUnitAtScreen(Current, Picked))
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
			if (ProjectWorldLocationToScreen(FVector(Info.BodyCenterCm.X, Info.BodyCenterCm.Y, 100.f), Proj)
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
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (!Registry || !Visuals)
	{
		return;
	}

	// Movement is bounded by the plugin's flat cost-grid tile (256 m at the
	// origin); clamp destinations into it with a small margin.
	auto ClampToGrid = [](const FVector2D& P) -> FVector2D
	{
		const float Limit = 12500.f; // cm (125 m of the 128 m half-tile)
		return FVector2D(FMath::Clamp(P.X, -Limit, Limit), FMath::Clamp(P.Y, -Limit, Limit));
	};

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
				Registry->IssueMoveOrder(Seg.Handle, ClampToGrid(Seg.AnchorCm), Seg.FacingYawDeg,
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
	const FVector2D Dest = ClampToGrid(DragCurrentWorldCm);

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
		Registry->IssueMoveOrder(H, ClampToGrid(UnitDest), Info.FacingYawDeg);
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoOrder Move Units=%d"), SelectedUnits.Num());
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
