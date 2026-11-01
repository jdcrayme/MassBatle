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
		Resolve(TEXT("IA_Demo_Halt")),    Resolve(TEXT("IA_Demo_Guard")),
		Resolve(TEXT("IA_Demo_Fire")),    Resolve(TEXT("IA_Demo_Skirmish")),
		Resolve(TEXT("IA_Demo_ExitGarrison")),
		Resolve(TEXT("IA_Demo_ZoomIn")),
		Resolve(TEXT("IA_Demo_ZoomOut")), Resolve(TEXT("IA_Demo_PanFwd")),
		Resolve(TEXT("IA_Demo_PanBack")), Resolve(TEXT("IA_Demo_PanLeft")),
		Resolve(TEXT("IA_Demo_PanRight")), Resolve(TEXT("IA_Demo_RotLeft")),
		Resolve(TEXT("IA_Demo_RotRight")),
	};

	// The 11 PR-2 actions stay all-or-nothing (PR-2's rule: a partially bound
	// controller is worse than the fallback table). The 4 PR-3 stance actions
	// are LENIENT: until their IA assets are authored (an editor session --
	// none was available this run), the missing ones are constructed at
	// runtime and mapped onto the LOADED IMC copy, so the authored 11 keep
	// their asset-driven retunability and the stances still bind.
	const UInputAction* Core[] = { OutActions.Select, OutActions.Order, OutActions.Halt,
		OutActions.ZoomIn, OutActions.ZoomOut, OutActions.PanFwd, OutActions.PanBack,
		OutActions.PanLeft, OutActions.PanRight, OutActions.RotLeft, OutActions.RotRight };
	for (const UInputAction* A : Core)
	{
		if (!A)
		{
			UE_LOG(LogDemoBattle, Warning,
				TEXT("EXEC_RESULT DemoInput IMC_Demo missing action(s) -- programmatic fallback in use (PR-2 rollback path)"));
			return false;
		}
	}
	int32 Filled = 0;
	// Constructs the stance action at runtime when the asset lacks it; the
	// runtime mapping lands on the LOADED IMC copy (the saved asset is
	// untouched -- the authoring session adds the real IA_Demo_* assets).
	const auto FillStance = [this, &Filled](const UInputAction*& Slot, const TCHAR* FallbackName,
		const TCHAR* Display, FKey Key) -> void
	{
		if (Slot)
		{
			return; // authored in the IMC asset
		}
		UInputAction* IA = NewObject<UInputAction>(this, FName(FallbackName));
		IA->ValueType = EInputActionValueType::Boolean;
		IA->ActionDescription = FText::FromString(Display);
		OwnedActions.Add(IA);
		DemoIMC->MapKey(IA, Key);
		Slot = IA;
		++Filled;
	};
	FillStance(OutActions.Guard, TEXT("IA_DemoGuard"),
		TEXT("Guard mode toggle"), EKeys::G);
	FillStance(OutActions.Fire, TEXT("IA_DemoFire"),
		TEXT("Fire-at-will toggle"), EKeys::F);
	FillStance(OutActions.Skirmish, TEXT("IA_DemoSkirmish"),
		TEXT("Skirmish toggle"), EKeys::R);
	FillStance(OutActions.ExitGarrison, TEXT("IA_DemoExitGarrison"),
		TEXT("Exit garrison"), EKeys::X);
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoInput IMC_Demo loaded actions=15 stance-assets-pending=%d"),
		Filled);
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
	OutActions.Guard = Bool(TEXT("IA_DemoGuard"), TEXT("Guard mode toggle"));
	OutActions.Fire = Bool(TEXT("IA_DemoFire"), TEXT("Fire-at-will toggle"));
	OutActions.Skirmish = Bool(TEXT("IA_DemoSkirmish"), TEXT("Skirmish toggle"));
	OutActions.ExitGarrison = Bool(TEXT("IA_DemoExitGarrison"), TEXT("Exit garrison"));
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
	AddMapping(DemoIMC, OutActions.Guard, EKeys::G);
	AddMapping(DemoIMC, OutActions.Fire, EKeys::F);
	AddMapping(DemoIMC, OutActions.Skirmish, EKeys::R);
	AddMapping(DemoIMC, OutActions.ExitGarrison, EKeys::X);
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
	IC->BindAction(A.Guard, ETriggerEvent::Started, this, &ADemoPlayerController::OnGuardToggle);
	IC->BindAction(A.Fire, ETriggerEvent::Started, this, &ADemoPlayerController::OnFireToggle);
	IC->BindAction(A.Skirmish, ETriggerEvent::Started, this, &ADemoPlayerController::OnSkirmishToggle);
	IC->BindAction(A.ExitGarrison, ETriggerEvent::Started, this, &ADemoPlayerController::OnExitGarrison);
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
	PollControlGroups();

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
		// PR-2 shoring: picking tracks the visual blob (the trimmed entity
		// centroid), not the sim's formation anchor.
		if (!ProjectWorldLocationToScreen(FVector(Info.VisualCentroidCm.X, Info.VisualCentroidCm.Y, DemoUi::BodyLabelZCm), Proj))
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
	// PR-3: the command bar consumes first -- the army card row and the
	// selected panel sit above the world; a press there never starts a
	// world marquee (and the release is short-circuited via bCommandBarPress).
	int32 CardIdx = INDEX_NONE;
	DemoCommandBar::EAction BarAction;
	if (HitCommandBar(FVector2D(MouseX, MouseY), CardIdx, BarAction))
	{
		bCommandBarPress = true;
		if (CardIdx != INDEX_NONE)
		{
			OnCardClicked(CardIdx);
		}
		else
		{
			ApplyStance(BarAction, SelectedUnits);
		}
		return;
	}
	bCommandBarPress = false;
	bMarqueeActive = true;
	MarqueeStartScreen = FVector2D(MouseX, MouseY);
}

void ADemoPlayerController::OnLMBReleased()
{
	if (bCommandBarPress)
	{
		bCommandBarPress = false; // consumed by the command bar press
		return;
	}
	if (!bMarqueeActive)
	{
		return;
	}
	bMarqueeActive = false;

	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Visuals || !Registry)
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
		// -- PR-2 banner clicks FIRST (the M2TW world layer): the banners are
		// the primary pick surface; the world pick below stays the fallback
		// for clicks between banners. Friendly banner: select (shift toggles;
		// double-click selects all units of that EDU type). Enemy banner with
		// a non-empty selection: ATTACK it (the same verb the RMB-on-enemy
		// path issues). --
		FUnitHandle BannerUnit;
		if (Visuals->FindBannerAtScreen(Current, BannerUnit))
		{
			const EDemoTeam BannerTeam = Visuals->GetTeam(BannerUnit);
			if (BannerTeam == EDemoTeam::Player)
			{
				const double Now = FPlatformTime::Seconds();
					const bool bDoubleClick = LastBannerClick == BannerUnit
						&& (Now - LastBannerClickTime) < 0.35;
					LastBannerClickTime = Now;
					LastBannerClick = BannerUnit;

					TArray<FUnitHandle> TypeGroup;
					if (bDoubleClick)
					{
						// Select all friendly units of the same EDU type (the spec's
						// D.16 double-click; grouped by the spawn spec's row).
						CollectSameTypeUnits(BannerUnit, TypeGroup);
						UE_LOG(LogDemoBattle, Display,
							TEXT("EXEC_RESULT DemoOrder BannerSelectType Units=%d"), TypeGroup.Num());
					}
				if (IsInputKeyDown(EKeys::LeftShift))
				{
					if (NewSelection.Find(BannerUnit) != INDEX_NONE)
					{
						NewSelection.Remove(BannerUnit);
					}
					else
					{
						NewSelection.Add(BannerUnit);
					}
				}
				else
				{
					NewSelection = bDoubleClick ? TypeGroup : TArray<FUnitHandle>{ BannerUnit };
				}
				SetSelection(NewSelection);
				return;
			}

			// Enemy banner + a live selection = the attack gesture.
			if (NewSelection.Num() > 0)
			{
				UUnitRegistrySubsystem* RegistrySys = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
				int32 Issued = 0;
				if (RegistrySys)
				{
					for (const FUnitHandle& H : NewSelection)
					{
						if (H != BannerUnit && RegistrySys->IssueAttackUnitOrder(H, BannerUnit))
						{
							PlayerOrderedUnits.Add(H);
							++Issued;
						}
					}
				}
				UE_LOG(LogDemoBattle, Display,
					TEXT("EXEC_RESULT DemoOrder BannerAttack Issued=%d of %d Target=%u:%u"),
					Issued, NewSelection.Num(), BannerUnit.Index, BannerUnit.Serial);
			}
			return; // an enemy banner click never changes the selection
		}

		FUnitHandle Picked;
		EDemoTeam PlayerTeam = EDemoTeam::Player;
		if (PickUnitAtScreen(Current, Picked, &PlayerTeam))
		{
			// PR-4: the world-pick double-click selects all units of the same
			// EDU type (the same verb the banner/card double-clicks run).
			const double Now = FPlatformTime::Seconds();
			const bool bDoubleClick = LastWorldClick == Picked
				&& (Now - LastWorldClickTime) < DoubleClickSec;
			LastWorldClickTime = Now;
			LastWorldClick = Picked;
			if (bDoubleClick)
			{
				TArray<FUnitHandle> TypeGroup;
				CollectSameTypeUnits(Picked, TypeGroup);
				if (TypeGroup.Num() > 0)
				{
					SetSelection(TypeGroup);
					UE_LOG(LogDemoBattle, Display,
						TEXT("EXEC_RESULT DemoOrder SelectType Units=%d"), TypeGroup.Num());
					return;
				}
			}
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
			// PR-2 shoring: the marquee selects on the visual blob.
			if (ProjectWorldLocationToScreen(FVector(Info.VisualCentroidCm.X, Info.VisualCentroidCm.Y, DemoUi::BodyLabelZCm), Proj)
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
				PlayerOrderedUnits.Add(H);
				++Issued;
			}
		}
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoOrder Garrison Issued=%d of %d Structure=%u At=(%.1f,%.1f)m"),
			Issued, SelectedUnits.Num(), StructureId,
			DragCurrentWorldCm.X * 0.01f, DragCurrentWorldCm.Y * 0.01f);
		SetOrderFlag();
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
		SetOrderFlag();
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
				PlayerOrderedUnits.Add(Seg.Handle);
			}
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoOrder FormationDrag Units=%d"), Segments.Num());
			SetOrderFlag();
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
		PlayerOrderedUnits.Add(H);
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoOrder Move Units=%d"), SelectedUnits.Num());
	SetOrderFlag();
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
	// assign pass fans the pairs out at ring distance). PR-4: a DOUBLE-RMB on
	// the same enemy is the CHARGE gesture (the spec section 4.1) -- the same
	// attack order plus the guard clear per attacker (chases, auto-charges,
	// pursues routers; UChargeProcessor seeds the bonus on run-up + fresh
	// contact, so the only delta IS the guard clear).
	FUnitHandle Enemy;
	EDemoTeam EnemyTeam = EDemoTeam::Enemy;
	if (PickUnitAtScreen(ScreenPos, Enemy, &EnemyTeam) && EnemyTeam != MyTeam)
	{
		const double Now = FPlatformTime::Seconds();
		const bool bCharge = LastAttackClick == Enemy
			&& (Now - LastAttackClickTime) < DoubleClickSec;
		LastAttackClickTime = Now;
		LastAttackClick = Enemy;

		int32 Issued = 0;
		for (const FUnitHandle& H : SelectedUnits)
		{
			if (H != Enemy && Registry->IssueAttackUnitOrder(H, Enemy))
			{
				PlayerOrderedUnits.Add(H);
				++Issued;
			}
		}
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoOrder AttackUnit Issued=%d of %d Target=%u:%u%s"),
			Issued, SelectedUnits.Num(), Enemy.Index, Enemy.Serial, bCharge ? TEXT(" (charge)") : TEXT(""));
		if (bCharge)
		{
			int32 GuardCleared = 0;
			for (const FUnitHandle& H : SelectedUnits)
			{
				if (H != Enemy && Registry->SetUnitGuardToggle(H, false))
				{
					++GuardCleared;
				}
			}
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoOrder Charge Issued=%d GuardCleared=%d"), Issued, GuardCleared);
		}
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
				PlayerOrderedUnits.Add(H);
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
			PlayerOrderedUnits.Add(H);
		}
		if (SelectedUnits.Num() > 0)
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoOrder Halt Units=%d"), SelectedUnits.Num());
		}
	}
}

// -- PR-3 command bar (demo-shell spec section 4.3) -----------------------------

int32 ADemoPlayerController::ApplyStance(DemoCommandBar::EAction Action, const TArray<FUnitHandle>& Targets)
{
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (!Registry || Targets.Num() == 0)
	{
		UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoStance FAIL no-targets"));
		return 0;
	}

	int32 Issued = 0;
	switch (Action)
	{
	case DemoCommandBar::EAction::Guard:
	{
		// The flip target reads off the FIRST target; the whole selection is
		// set to the same value (uniform stance, the TW panel semantics).
		const FUnitRuntimeState* First = Registry->FindUnitState(Targets[0]);
		const bool bTarget = !(First && First->bGuardMode);
		for (const FUnitHandle& H : Targets)
		{
			if (Registry->SetUnitGuardToggle(H, bTarget))
			{
				++Issued;
			}
		}
		const FUnitRuntimeState* ReadBack = Registry->FindUnitState(Targets[0]);
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoStance Guard Target=%d Issued=%d of %d ReadBack=%d"),
			bTarget ? 1 : 0, Issued, Targets.Num(),
			(ReadBack && ReadBack->bGuardMode) ? 1 : 0);
		break;
	}
	case DemoCommandBar::EAction::FireAtWill:
	case DemoCommandBar::EAction::Skirmish:
	{
		// Per-unit flip: each unit keeps its own other toggle (GetUnitFireToggles
		// read-back drives the write -- the spec's "read-back state from the
		// getters"). Out-of-ammo units still flip (the fire pass simply never
		// fires); the panel grays instead (D.15).
		const bool bFire = Action == DemoCommandBar::EAction::FireAtWill;
		for (const FUnitHandle& H : Targets)
		{
			bool bFAW = true, bSkirm = false, bOA = false;
			Registry->GetUnitFireToggles(H, bFAW, bSkirm, bOA);
			if (Registry->SetUnitFireToggles(H, bFire ? !bFAW : bFAW, bFire ? bSkirm : !bSkirm))
			{
				++Issued;
			}
		}
		bool bFAW = false, bSkirm = false, bOA = false;
		Registry->GetUnitFireToggles(Targets[0], bFAW, bSkirm, bOA);
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoStance %s Issued=%d of %d ReadBack0 faw=%d skirm=%d oa=%d"),
			bFire ? TEXT("Fire") : TEXT("Skirmish"), Issued, Targets.Num(), bFAW ? 1 : 0, bSkirm ? 1 : 0, bOA ? 1 : 0);
		break;
	}
	case DemoCommandBar::EAction::ExitGarrison:
	{
		for (const FUnitHandle& H : Targets)
		{
			if (Registry->IssueExitGarrisonOrder(H))
			{
				++Issued;
			}
		}
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoStance ExitGarrison Issued=%d of %d"), Issued, Targets.Num());
		break;
	}
	case DemoCommandBar::EAction::Wider:
	case DemoCommandBar::EAction::Narrower:
	{
		// Wider / Narrower (spec section 4.3): re-issue the move order at the
		// unit's CURRENT anchor through the same A2 width path the formation
		// drag uses; rank depth derives from files x roster (the TW behavior).
		// Garrisoned units hold their structure (their order is the garrison).
		constexpr int32 WidthStepFiles = 2;
		for (const FUnitHandle& H : Targets)
		{
			const FUnitOrder* Order = Registry->FindOrder(H);
			if (Order && (Order->Type == FUnitOrder::EType::Garrison || Order->Type == FUnitOrder::EType::ExitGarrison))
			{
				continue;
			}
			FDemoUnitBodyInfo Info;
			if (!Visuals || !Visuals->GetUnitBodyInfo(H, Info) || Info.SoldierCount <= 0)
			{
				continue;
			}
			const int32 Files = FMath::Clamp(
				Info.NumFiles + (Action == DemoCommandBar::EAction::Wider ? WidthStepFiles : -WidthStepFiles),
				1, Info.SoldierCount);
			if (Files == Info.NumFiles)
			{
				continue;
			}
			if (Registry->IssueMoveOrder(H, ClampToGridHelper(Info.BodyCenterCm), Info.FacingYawDeg, /*speed*/ 0.f, Files))
			{
				PlayerOrderedUnits.Add(H);
				++Issued;
			}
		}
		FDemoUnitBodyInfo ReadBack;
		int32 NewFiles = 0, NewRanks = 0;
		if (Visuals && Visuals->GetUnitBodyInfo(Targets[0], ReadBack))
		{
			NewFiles = ReadBack.NumFiles;
			NewRanks = FMath::Max(1, FMath::CeilToInt(static_cast<float>(FMath::Max(ReadBack.SoldierCount, 1)) / FMath::Max(ReadBack.NumFiles, 1)));
		}
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoStance %s Issued=%d of %d Files0=%d Ranks0=%d"),
			Action == DemoCommandBar::EAction::Wider ? TEXT("Wider") : TEXT("Narrower"),
			Issued, Targets.Num(), NewFiles, NewRanks);
		break;
	}
	default:
		break;
	}
	return Issued;
}

bool ADemoPlayerController::HitCommandBar(const FVector2D& ScreenPos, int32& OutCardIndex,
	DemoCommandBar::EAction& OutAction) const
{
	OutCardIndex = INDEX_NONE;
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (!Visuals || !GEngine || !GEngine->GameViewport)
	{
		return false;
	}
	FVector2D ViewSize(FVector2D::ZeroVector);
	GEngine->GameViewport->GetViewportSize(ViewSize);
	if (ViewSize.X <= 0.f || ViewSize.Y <= 0.f)
	{
		return false;
	}

	// Panel buttons first (the panel draws on top; its hit tests only run
	// while it is visible -- a selection exists).
	if (SelectedUnits.Num() > 0)
	{
		for (uint8 A = 0; A < static_cast<uint8>(DemoCommandBar::EAction::Count); ++A)
		{
			const DemoCommandBar::EAction Action = static_cast<DemoCommandBar::EAction>(A);
			if (DemoCommandBar::ActionRect(Action, ViewSize.Y).IsInside(ScreenPos))
			{
				OutAction = Action;
				return true;
			}
		}
	}
	// The army card row: the SAME GetUnitsOfTeam list the HUD draws.
	TArray<FUnitHandle> PlayerUnits;
	Visuals->GetUnitsOfTeam(EDemoTeam::Player, PlayerUnits);
	const int32 Cards = FMath::Min(PlayerUnits.Num(), DemoCommandBar::MaxCards);
	for (int32 i = 0; i < Cards; ++i)
	{
		if (DemoCommandBar::CardRect(i, ViewSize.Y).IsInside(ScreenPos))
		{
			OutCardIndex = i;
			return true;
		}
	}
	return false;
}

void ADemoPlayerController::OnCardClicked(int32 CardIndex)
{
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (!Visuals)
	{
		return;
	}
	// The card list is the HUD's list (registration order, both sides read
	// GetUnitsOfTeam) -- the index maps 1:1.
	TArray<FUnitHandle> PlayerUnits;
	Visuals->GetUnitsOfTeam(EDemoTeam::Player, PlayerUnits);
	if (!PlayerUnits.IsValidIndex(CardIndex))
	{
		return;
	}
	const FUnitHandle Clicked = PlayerUnits[CardIndex];
	const double Now = FPlatformTime::Seconds();
	const bool bDoubleClick = LastCardClick == Clicked && (Now - LastCardClickTime) < 0.35;
	LastCardClickTime = Now;
	LastCardClick = Clicked;

	TArray<FUnitHandle> NewSelection;
	if (bDoubleClick)
	{
		CollectSameTypeUnits(Clicked, NewSelection);
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoOrder CardSelectType Units=%d"), NewSelection.Num());
	}
	else if (IsInputKeyDown(EKeys::LeftShift))
	{
		NewSelection = SelectedUnits;
		const int32 Existing = NewSelection.Find(Clicked);
		if (Existing != INDEX_NONE)
		{
			NewSelection.RemoveAt(Existing);
		}
		else
		{
			NewSelection.Add(Clicked);
		}
	}
	else
	{
		NewSelection = TArray<FUnitHandle> { Clicked };
	}
	SetSelection(NewSelection);
	if (!bDoubleClick)
	{
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoOrder CardSelect Units=%d Index=%d"), NewSelection.Num(), CardIndex);
	}
}

void ADemoPlayerController::CollectSameTypeUnits(FUnitHandle Seed, TArray<FUnitHandle>& Out) const
{
	Out.Reset();
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UDemoUnitVisualSubsystem* Visuals = VisualSubsystem();
	if (!Registry || !Visuals)
	{
		return;
	}
	const FUnitSpawnSpec* SeedSpec = Registry->FindUnitSpec(Seed);
	if (!SeedSpec)
	{
		return;
	}
	TArray<FUnitHandle> PlayerUnits;
	Visuals->GetUnitsOfTeam(EDemoTeam::Player, PlayerUnits);
	for (const FUnitHandle& H : PlayerUnits)
	{
		const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(H);
		if (Spec && Spec->EduRowIndex == SeedSpec->EduRowIndex)
		{
			Out.Add(H);
		}
	}
}

// -- PR-4 gestures --------------------------------------------------------------

void ADemoPlayerController::PollControlGroups()
{
	// Ctrl-groups (the spec section 4.1): Ctrl+1..9 stores the selection,
	// 1..9 recalls. The number keys are not in the IMC -- the F3 precedent:
	// edge-latched IsInputKeyDown polling for fixed-convention keys. Both the
	// top row and the numpad drive the same slot (a press of either latches).
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	static const FKey TopRow[9] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	static const FKey NumRow[9] = { EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour,
		EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
	const bool bCtrl = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
	for (int32 Slot = 1; Slot <= 9; ++Slot)
	{
		const bool bDown = IsInputKeyDown(TopRow[Slot - 1]) || IsInputKeyDown(NumRow[Slot - 1]);
		if (!bDown)
		{
			LatchedGroupKeys.Remove(Slot);
			continue;
		}
		if (LatchedGroupKeys.Contains(Slot))
		{
			continue; // held from a previous tick
		}
		LatchedGroupKeys.Add(Slot);
		if (bCtrl)
		{
			ControlGroups.Add(Slot, SelectedUnits);
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoOrder GroupStore Slot=%d Units=%d"), Slot, SelectedUnits.Num());
		}
		else
		{
			TArray<FUnitHandle> Group;
			int32 Stored = 0;
			if (const TArray<FUnitHandle>* Found = ControlGroups.Find(Slot))
			{
				Group = *Found;
				Stored = Found->Num();
			}
			if (Group.Num() == 0)
			{
				continue;
			}
			// Recall keeps the stored group (re-recallable); registry-stale
			// handles drop (the PruneStaleSelection check).
			Group.RemoveAll([Registry](const FUnitHandle& H) { return !Registry || !Registry->IsHandleValid(H); });
			if (Group.Num() > 0)
			{
				SetSelection(Group);
			}
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoOrder GroupRecall Slot=%d Kept=%d of %d"),
				Slot, Group.Num(), Stored);
		}
	}
}

bool ADemoPlayerController::GetOrderFlag(FVector2D& OutPointCm, float& OutAgeFrac) const
{
	const double Age = FPlatformTime::Seconds() - OrderFlagTime;
	if (Age > OrderFlagLifeSec)
	{
		return false;
	}
	OutPointCm = OrderFlagPointCm;
	OutAgeFrac = static_cast<float>(Age / OrderFlagLifeSec);
	return true;
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

// -- Demo.Stance console command (PR-3) -----------------------------------------
// The headless/PIE evidence channel for the command bar: runs the EXACT
// ApplyStance the G/F/R/X keys and the selected panel's buttons run, then
// logs the getter read-back. Shape: Demo.Stance <action> [delaySec=3]
// [unitIndex...] -- the 2nd arg is always the delay (the stance pass needs a
// settled registry: units spawn deferred), indices are 1-based registry
// units; no indices = all player-team units.
static FAutoConsoleCommandWithWorldAndArgs GDemoStance(
	TEXT("Demo.Stance"),
	TEXT("<guard|fire|skirmish|exit|wider|narrow> [delaySec=3] [unitIndex...] -- the PR-3 stance family (keys G/F/R/X + panel buttons share ApplyStance); no indices = all player units; EXEC_RESULT DemoStance"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (World == nullptr || Args.Num() < 1)
			{
				UE_LOG(LogDemoBattle, Display,
					TEXT("EXEC_RESULT DemoStance FAIL BadArgs (need: <guard|fire|skirmish|exit|wider|narrow> [delaySec=3] [unitIndex...])"));
				return;
			}
			DemoCommandBar::EAction Action;
			if (Args[0] == TEXT("guard")) { Action = DemoCommandBar::EAction::Guard; }
			else if (Args[0] == TEXT("fire")) { Action = DemoCommandBar::EAction::FireAtWill; }
			else if (Args[0] == TEXT("skirmish")) { Action = DemoCommandBar::EAction::Skirmish; }
			else if (Args[0] == TEXT("exit")) { Action = DemoCommandBar::EAction::ExitGarrison; }
			else if (Args[0] == TEXT("wider")) { Action = DemoCommandBar::EAction::Wider; }
			else if (Args[0] == TEXT("narrow")) { Action = DemoCommandBar::EAction::Narrower; }
			else
			{
				UE_LOG(LogDemoBattle, Display,
					TEXT("EXEC_RESULT DemoStance FAIL BadAction=%s"), *Args[0]);
				return;
			}
			const float Delay = Args.Num() > 1 ? FMath::Max(0.f, FCString::Atof(*Args[1])) : 3.f;
			TArray<int32> Indices;
			for (int32 i = 2; i < Args.Num(); ++i)
			{
				Indices.Add(FCString::Atoi(*Args[i]));
			}

			TWeakObjectPtr<UWorld> WeakWorld = World;
			FTimerHandle StanceHandle;
			World->GetTimerManager().SetTimer(StanceHandle,
				FTimerDelegate::CreateLambda([WeakWorld, Action, Indices]()
				{
					UWorld* W = WeakWorld.Get();
					ADemoPlayerController* PC = W ? Cast<ADemoPlayerController>(W->GetFirstPlayerController()) : nullptr;
					UUnitRegistrySubsystem* Registry = W ? W->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
					UDemoUnitVisualSubsystem* Visuals = W ? W->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
					if (!PC || !Registry || !Visuals)
					{
						UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoStance FAIL no-subsystems"));
						return;
					}
					// Target units: explicit 1-based registry indices, else the
					// whole player team (the headless default -- no selection).
					TArray<FUnitHandle> Targets;
					if (Indices.Num() > 0)
					{
						for (const FUnitHandle& Handle : Registry->GetSortedHandles())
						{
							for (int32 Idx : Indices)
							{
								if (Handle.Index == static_cast<uint32>(Idx))
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
					if (Targets.Num() == 0)
					{
						UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoStance FAIL no-targets"));
						return;
					}
					PC->ApplyStance(Action, Targets);
				}),
				FMath::Max(0.1f, Delay), false);
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoStance armed action=%s delay=%.1f indices=%d"),
				*Args[0], Delay, Indices.Num());
		}));
// -- Demo.CommandBarTest console command (PR-3) ---------------------------------
// The one-command headless evidence run for the command bar: arms the whole
// stance sequence on internal timers (the Demo.AttackTest shape -- the
// multi-command -ExecCmds path proved unreliable in -game processes). Every
// step runs the EXACT ApplyStance the keys/buttons run and logs the getter
// read-back; the garrison step resolves the village building through the same
// footprint test the RMB click uses (the spec section 2.2 building at 8,52 m).
static FAutoConsoleCommandWithWorldAndArgs GDemoCommandBarTest(
	TEXT("Demo.CommandBarTest"),
	TEXT("PR-3 evidence: fire-off/on (all player units) -> skirmish P4 -> guard P2 -> wider+narrow P2 -> garrison P3 -> egress P3; EXEC_RESULT DemoStance/DemoGarrison lines"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& /*Args*/, UWorld* World)
		{
			if (World == nullptr)
			{
				return;
			}
			const TWeakObjectPtr<UWorld> WeakWorld = World;
			// (at seconds, action, unit index or 0 = all player units).
			// The egress step waits for P3's ingress to SETTLE: the village
			// building stands ~50 m from P3's line (approach ~9 sim-s) and the
			// 40-man roster files through the entrance for a while after, so
			// the exit fires at t=120 -- the t=20 exit landed as Phase=0
			// CANCEL (mid-approach) and the t=56 one as Ingress->Egress
			// (measured 2026-11-01; the -benchmark window is ~wall-cheap).
			const struct { float At; DemoCommandBar::EAction Action; int32 UnitIdx; } Steps[] =
			{
				{  3.f, DemoCommandBar::EAction::FireAtWill,   0 }, // off (read-back faw=0)
				{  5.f, DemoCommandBar::EAction::FireAtWill,   0 }, // back on (faw=1)
				{  6.f, DemoCommandBar::EAction::Skirmish,     5 }, // P4 the archers
				{  7.f, DemoCommandBar::EAction::Guard,        2 }, // P2 guard on
				{  8.f, DemoCommandBar::EAction::Wider,        2 },
				{ 10.f, DemoCommandBar::EAction::Narrower,     2 },
				{ 12.f, DemoCommandBar::EAction::ExitGarrison, 0 }, // egress no-op line (nothing garrisoned yet)
				{ 120.f, DemoCommandBar::EAction::ExitGarrison, 3 }, // P3 egresses after settling in
			};
			for (const auto& Step : Steps)
			{
				FTimerHandle StepHandle;
				World->GetTimerManager().SetTimer(StepHandle,
					FTimerDelegate::CreateLambda([WeakWorld, Action = Step.Action, UnitIdx = Step.UnitIdx]()
					{
						UWorld* W = WeakWorld.Get();
						ADemoPlayerController* PC = W ? Cast<ADemoPlayerController>(W->GetFirstPlayerController()) : nullptr;
						UDemoUnitVisualSubsystem* Visuals = W ? W->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
						if (!PC || !Visuals)
						{
							UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoStance FAIL no-subsystems"));
							return;
						}
						TArray<FUnitHandle> Targets;
						if (UnitIdx > 0)
						{
							for (const FUnitHandle& Handle : Visuals->GetAllUnits())
							{
								if (Handle.Index == static_cast<uint32>(UnitIdx))
								{
									Targets.Add(Handle);
								}
							}
						}
						else
						{
							Visuals->GetUnitsOfTeam(EDemoTeam::Player, Targets);
						}
						PC->ApplyStance(Action, Targets);
					}),
					Step.At, false);
			}

			// The garrison step (t=14, between wider/narrow and the egress):
			// the same resolution the RMB click path runs, then the P3 order.
			FTimerHandle GarrisonHandle;
			World->GetTimerManager().SetTimer(GarrisonHandle,
				FTimerDelegate::CreateLambda([WeakWorld]()
				{
					UWorld* W = WeakWorld.Get();
					if (!W)
					{
						return;
					}
					// The M7 village building's authored center (spec section 2.2).
					const FVector2D VillageCm(8.f * 100.f, 52.f * 100.f);
					const uint32 StructureId = ADemoPlayerController::FindStructureAtWorld(W, VillageCm);
					UUnitRegistrySubsystem* Registry = W->GetSubsystem<UUnitRegistrySubsystem>();
					UDemoUnitVisualSubsystem* Visuals = W->GetSubsystem<UDemoUnitVisualSubsystem>();
					if (StructureId == 0 || !Registry || !Visuals)
					{
						UE_LOG(LogDemoBattle, Display,
							TEXT("EXEC_RESULT DemoCommandBarTest Garrison MISS (no structure at 8,52)"));
						return;
					}
					int32 Issued = 0;
					for (const FUnitHandle& Handle : Visuals->GetAllUnits())
					{
						if (Handle.Index == 3 && Registry->IssueGarrisonOrder(Handle, StructureId))
						{
							++Issued;
						}
					}
					UE_LOG(LogDemoBattle, Display,
						TEXT("EXEC_RESULT DemoCommandBarTest Garrison Issued=%d Structure=%u"), Issued, StructureId);
				}),
				14.f, false);
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoCommandBarTest armed 8 steps + garrison"));

			// Self-termination (the boot's ONE -ExecCmds command -- the
			// multi-command chains are unreliable in -game processes, measured
			// 2026-11-01): the egress settles by t~26, then quit. The
			// t.FPSChart.DurationAndExit exit path needs its own command slot.
			FTimerHandle QuitHandle;
			World->GetTimerManager().SetTimer(QuitHandle,
				FTimerDelegate::CreateLambda([WeakWorld]()
				{
					UWorld* W = WeakWorld.Get();
					UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoCommandBarTest quit"));
					if (GEngine && W)
					{
						GEngine->Exec(W, TEXT("Quit"));
					}
					FPlatformMisc::RequestExit(0);
				}),
				150.f, false);
		}));

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
