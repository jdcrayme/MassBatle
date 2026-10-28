// DemoHUD.cpp -- see header.
#include "DemoHUD.h"
#include "DemoPlayerController.h"
#include "DemoUnitVisualSubsystem.h"
#include "BattleRegistry.h"
#include "DemoUnitVisualSubsystem.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ADemoHUD::ADemoHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

FVector2D ADemoHUD::Project(const FVector2D& PosCm, float Zcm) const
{
	FVector2D Screen(FVector2D::ZeroVector);
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		PC->ProjectWorldLocationToScreen(FVector(PosCm.X, PosCm.Y, Zcm), Screen);
	}
	return Screen;
}

void ADemoHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	DrawControlsHint();
	DrawUnitLabels();
	DrawSelectionCircles();
	DrawMarquee();
	DrawOrderDragPreview();
	DrawUnitCards();
}

void ADemoHUD::DrawUnitCards()
{
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(PlayerOwner);
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UDemoUnitVisualSubsystem* Visuals = GetWorld() ? GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
	if (!PC || !Registry || !Visuals || !Canvas || !GEngine || !GEngine->GetMediumFont())
	{
		return;
	}
	const TArray<FUnitHandle>& Selected = PC->GetSelectedUnits();
	if (Selected.Num() == 0)
	{
		return;
	}

	// Card geometry: bottom-left row, 130x58 px, 6 px gap; at most 10 cards
	// drawn (the tail is a "+N" card). The selection IS the card set.
	constexpr float CardW = 130.f;
	constexpr float CardH = 58.f;
	constexpr float Gap = 6.f;
	constexpr int32 MaxCards = 10;
	const float RowY = static_cast<float>(Canvas->SizeY) - CardH - 14.f;

	const int32 Cards = FMath::Min(Selected.Num(), MaxCards);
	for (int32 i = 0; i < Cards; ++i)
	{
		const FUnitHandle& H = Selected[i];
		const float X = 14.f + i * (CardW + Gap);
		const FLinearColor TeamColor = Visuals->GetTeam(H) == EDemoTeam::Enemy
			? FLinearColor(0.9f, 0.25f, 0.2f)
			: FLinearColor(0.2f, 0.55f, 0.95f);

		// Card body + team bar.
		DrawRect(FLinearColor(0.06f, 0.06f, 0.08f, 0.82f), X, RowY, CardW, CardH);
		DrawRect(TeamColor, X, RowY, 4.f, CardH);
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.25f), X, RowY, CardW, 1.f);

		// Spec name (the EDU type) + alive roster.
		FString Name = TEXT("unit");
		int32 Alive = 0;
		if (const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(H))
		{
			Name = Spec->UnitName;
		}
		Alive = Registry->GetRosterSize(H);
		Canvas->SetDrawColor(235, 235, 235, 242);
		Canvas->DrawText(GEngine->GetMediumFont(), Name, X + 10.f, RowY + 8.f, 0.62f, 0.62f);
		Canvas->SetDrawColor(165, 215, 165, 230);
		Canvas->DrawText(GEngine->GetMediumFont(),
			FString::Printf(TEXT("alive %d"), Alive), X + 10.f, RowY + 30.f, 0.62f, 0.62f);
	}
	if (Selected.Num() > MaxCards)
	{
		const float X = 14.f + MaxCards * (CardW + Gap);
		DrawRect(FLinearColor(0.06f, 0.06f, 0.08f, 0.82f), X, RowY, 44.f, CardH);
		Canvas->SetDrawColor(230, 230, 230, 230);
		Canvas->DrawText(GEngine->GetMediumFont(),
			FString::Printf(TEXT("+%d"), Selected.Num() - MaxCards), X + 10.f, RowY + 20.f, 0.7f, 0.7f);
	}
}

void ADemoHUD::DrawControlsHint()
{
	if (!GEngine || !GEngine->GetMediumFont())
	{
		return;
	}
	static const TCHAR* Lines[] =
	{
		TEXT("LMB click / shift+click  - select / toggle unit"),
		TEXT("LMB drag                 - marquee select"),
		TEXT("RMB click                - move (keeps formation)"),
		TEXT("RMB on ENEMY unit        - ATTACK (selection attacks the target)"),
		TEXT("Ctrl+RMB on ground       - attack-move (march, engage en route)"),
		TEXT("RMB on structure         - garrison (units file in)"),
		TEXT("RMB drag                 - formation: width + facing (drag line = front)"),
		TEXT("H                        - halt        WASD/arrows - pan"),
		TEXT("Q/E - rotate             - mouse wheel - zoom"),
	};
	FVector2D Pos(20.f, 20.f);
	Canvas->SetDrawColor(216, 216, 216, 204);
	for (const TCHAR* Line : Lines)
	{
		Canvas->DrawText(GEngine->GetMediumFont(), Line, Pos.X, Pos.Y, 0.8f, 0.8f);
		Pos.Y += 18.f;
	}
}

void ADemoHUD::DrawUnitLabels()
{
	UDemoUnitVisualSubsystem* Visuals = GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>();
	if (!Visuals || !GEngine || !GEngine->GetMediumFont())
	{
		return;
	}
	for (const FUnitHandle& Handle : Visuals->GetAllUnits())
	{
		FDemoUnitBodyInfo Info;
		if (!Visuals->GetUnitBodyInfo(Handle, Info))
		{
			continue;
		}
		const FVector2D Screen = Project(Info.BodyCenterCm);
		const FString Label = FString::Printf(TEXT("%s  (%d)"),
			Info.UnitName.IsEmpty() ? TEXT("Unit") : *Info.UnitName, Info.SoldierCount);
		Canvas->SetDrawColor(Visuals->GetTeam(Handle) == EDemoTeam::Player ? 255 : 255,
			Visuals->GetTeam(Handle) == EDemoTeam::Player ? 255 : 153,
			Visuals->GetTeam(Handle) == EDemoTeam::Player ? 255 : 140, 255);
		Canvas->DrawText(GEngine->GetMediumFont(), Label, Screen.X - 40.f, Screen.Y - 34.f, 0.7f, 0.7f);
	}
}

void ADemoHUD::DrawSelectionCircles()
{
	UDemoUnitVisualSubsystem* Visuals = GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>();
	if (!Visuals)
	{
		return;
	}
	for (const FUnitHandle& Handle : Visuals->GetAllUnits())
	{
		if (!Visuals->IsSelected(Handle))
		{
			continue;
		}
		FDemoUnitBodyInfo Info;
		if (!Visuals->GetUnitBodyInfo(Handle, Info))
		{
			continue;
		}
		// Ring sized to the unit's actual frontage.
		const float RadiusCm = FMath::Max(Info.NumFiles * Info.FileSpacingM * 100.f * 0.5f, 150.f) + 60.f;
		DrawWorldCircle(Info.BodyCenterCm, RadiusCm, FColor::Green);
	}
}

void ADemoHUD::DrawWorldCircle(const FVector2D& CenterCm, float RadiusCm, const FColor& Color, int32 Segments)
{
	FVector2D Prev = Project(CenterCm + FVector2D(RadiusCm, 0.f));
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float A = FMath::DegreesToRadians(360.f * i / Segments);
		const FVector2D Next = Project(CenterCm + FVector2D(RadiusCm * FMath::Cos(A), RadiusCm * FMath::Sin(A)));
		DrawLine(Prev.X, Prev.Y, Next.X, Next.Y, Color, /*Thickness*/ 1.5f);
		Prev = Next;
	}
}

void ADemoHUD::DrawWorldFacingArrow(const FVector2D& PosCm, float YawDeg, float LengthCm, const FColor& Color)
{
	const float Rad = FMath::DegreesToRadians(YawDeg);
	const FVector2D Front(FMath::Cos(Rad), FMath::Sin(Rad));
	const FVector2D Tip = PosCm + Front * LengthCm;
	const FVector2D Base = Project(PosCm);
	const FVector2D TipS = Project(Tip);
	DrawLine(Base.X, Base.Y, TipS.X, TipS.Y, Color, 2.f);

	// Arrowhead perpendicular bar (TW-style facing tick).
	const FVector2D LeftTick = Tip - Front * (LengthCm * 0.25f) + FVector2D(-Front.Y, Front.X) * (LengthCm * 0.15f);
	const FVector2D RightTick = Tip - Front * (LengthCm * 0.25f) - FVector2D(-Front.Y, Front.X) * (LengthCm * 0.15f);
	const FVector2D LT = Project(LeftTick);
	const FVector2D RT = Project(RightTick);
	DrawLine(TipS.X, TipS.Y, LT.X, LT.Y, Color, 2.f);
	DrawLine(TipS.X, TipS.Y, RT.X, RT.Y, Color, 2.f);
}

void ADemoHUD::DrawMarquee()
{
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(PlayerOwner);
	if (!PC)
	{
		return;
	}
	FVector2D Start, Current;
	if (!PC->IsMarqueeActive(Start, Current))
	{
		return;
	}
	const FBox2D Box(FVector2D::Min(Start, Current), FVector2D::Max(Start, Current));
	DrawRect(FLinearColor(0.1f, 0.9f, 0.2f, 0.08f), Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y);
	DrawLine(Box.Min.X, Box.Min.Y, Box.Max.X, Box.Min.Y, FColor::Green);
	DrawLine(Box.Max.X, Box.Min.Y, Box.Max.X, Box.Max.Y, FColor::Green);
	DrawLine(Box.Max.X, Box.Max.Y, Box.Min.X, Box.Max.Y, FColor::Green);
	DrawLine(Box.Min.X, Box.Max.Y, Box.Min.X, Box.Min.Y, FColor::Green);
}

void ADemoHUD::DrawOrderDragPreview()
{
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(PlayerOwner);
	if (!PC || !GEngine || !GEngine->GetMediumFont())
	{
		return;
	}
	FVector2D StartScreen, CurrentScreen;
	if (!PC->IsOrderDragActive(StartScreen, CurrentScreen))
	{
		return;
	}

	UDemoUnitVisualSubsystem* Visuals = GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>();
	if (!Visuals)
	{
		return;
	}
	TArray<FDemoDragSegment> Segments;
	PC->ComputeDragLayout(Segments);

	for (const FDemoDragSegment& Seg : Segments)
	{
		FDemoUnitBodyInfo Info;
		if (!Visuals->GetUnitBodyInfo(Seg.Handle, Info))
		{
			continue;
		}
		const float Rad = FMath::DegreesToRadians(Seg.FacingYawDeg);
		const FVector2D Front(FMath::Cos(Rad), FMath::Sin(Rad));
		const FVector2D Right(-Front.Y, Front.X);
		const int32 Files = FMath::Max(1, Seg.Files);
		const int32 Ranks = FMath::Max(1, Seg.NumRanks);
		const float HalfW = Files * Info.FileSpacingM * 100.f * 0.5f;
		const float Depth = Ranks * Info.RankSpacingM * 100.f;

		// Footprint: front line on the drag anchor, body behind it.
		const FVector2D FL = Seg.AnchorCm - Right * HalfW;
		const FVector2D FR = Seg.AnchorCm + Right * HalfW;
		const FVector2D BL = FL - Front * Depth;
		const FVector2D BR = FR - Front * Depth;
		auto Line = [this](const FVector2D& A, const FVector2D& B, const FColor& C)
		{
			const FVector2D SA = Project(A);
			const FVector2D SB = Project(B);
			DrawLine(SA.X, SA.Y, SB.X, SB.Y, C, 2.f);
		};
		Line(FL, FR, FColor::Green);
		Line(FL, BL, FColor::White);
		Line(FR, BR, FColor::White);
		Line(BL, BR, FColor::White);
		DrawWorldFacingArrow(Seg.AnchorCm + Right * HalfW, Seg.FacingYawDeg, 160.f, FColor::Yellow);

		const FVector2D CenterScreen = Project(Seg.AnchorCm - Front * (Depth * 0.5f));
		Canvas->SetDrawColor(255, 224, 64, 255);
		Canvas->DrawText(GEngine->GetMediumFont(),
			FString::Printf(TEXT("%d x %d"), Files, Ranks),
			CenterScreen.X - 14.f, CenterScreen.Y - 8.f, 0.7f, 0.7f);
	}

	if (Segments.Num() == 0)
	{
		// No layout (click-order pending): draw a simple order marker at the cursor.
		float MouseX, MouseY;
		if (GetOwningPlayerController() && GetOwningPlayerController()->GetMousePosition(MouseX, MouseY))
		{
			DrawLine(MouseX - 8.f, MouseY, MouseX + 8.f, MouseY, FColor::Yellow, 1.5f);
			DrawLine(MouseX, MouseY - 8.f, MouseX, MouseY + 8.f, FColor::Yellow, 1.5f);
		}
	}
}
