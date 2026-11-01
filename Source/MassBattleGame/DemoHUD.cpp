// DemoHUD.cpp -- see header.
#include "DemoHUD.h"
#include "DemoGameMode.h"
#include "DemoPlayerController.h"
#include "DemoUnitVisualSubsystem.h"
#include "BattleRegistry.h"
#include "DemoUnitVisualSubsystem.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "BattleSpawnLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

// -- presentation helpers (the overlay's state-color family + glyphs) ----------
// The morale chip mirrors MoraleTickProcessor::DrawMoraleOverlay's state
// colors; the plugin's overlay stays the analytic home, this is the copy.

namespace
{
	FColor RoutStateColor(uint8 RoutState)
	{
		static const FColor StateColors[4] = {
			FColor(80, 220, 80),   // Steady: green
			FColor(240, 220, 60),  // Wavering: yellow
			FColor(240, 70, 50),   // Routing: red
			FColor(120, 20, 20)    // Shattered: dark red
		};
		return StateColors[FMath::Clamp(static_cast<int32>(RoutState), 0, 3)];
	}

	const TCHAR* RoutStateName(uint8 RoutState)
	{
		static const TCHAR* Names[4] = { TEXT("Steady"), TEXT("Wavering"), TEXT("Routing"), TEXT("Shattered") };
		return Names[FMath::Clamp(static_cast<int32>(RoutState), 0, 3)];
	}

	/** PR-3: the selected panel's discipline read (EBattleDiscipline). */
	const TCHAR* DisciplineName(uint8 Discipline)
	{
		static const TCHAR* Names[4] = { TEXT("Low"), TEXT("Normal"), TEXT("Disciplined"), TEXT("Impetuous") };
		return Names[FMath::Clamp(static_cast<int32>(Discipline), 0, 3)];
	}

	const TCHAR* GlyphFor(const FBattleRowStats* Stats)
	{
		if (!Stats)
		{
			return TEXT("?");
		}
		if (Stats->bCavalry) { return TEXT("CA"); }
		if (Stats->bBow) { return TEXT("MS"); }
		return TEXT("IN");
	}
}

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
	DrawBeatLabel();
	DrawUnitBanners();
	DrawSelectionCircles();
	DrawMarquee();
	DrawOrderDragPreview();
	DrawUnitCards();
	DrawSelectedPanel();
}

void ADemoHUD::DrawUnitCards()
{
	UDemoUnitVisualSubsystem* Visuals = GetWorld() ? GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
	if (!Visuals || !Canvas || !GEngine || !GEngine->GetMediumFont())
	{
		return;
	}

	// PR-3: the M2TW army card row -- ALL player units in registration order
	// (the PC's hit tests iterate the SAME GetUnitsOfTeam list, so every unit
	// gets a card even pre-spawn: alignment by construction). Selected cards
	// highlight; routing/shattered cards gray (the TW convention).
	TArray<FUnitHandle> PlayerUnits;
	Visuals->GetUnitsOfTeam(EDemoTeam::Player, PlayerUnits);
	if (PlayerUnits.Num() == 0)
	{
		return;
	}
	ADemoGameMode* GM = GetWorld() ? Cast<ADemoGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	const int32 Cards = FMath::Min(PlayerUnits.Num(), DemoCommandBar::MaxCards);
	for (int32 i = 0; i < Cards; ++i)
	{
		const FUnitHandle& H = PlayerUnits[i];
		const FBox2D Rect = DemoCommandBar::CardRect(i, static_cast<float>(Canvas->SizeY));
		FDemoUnitBodyInfo Info;
		const bool bHasBody = Visuals->GetUnitBodyInfo(H, Info);
		const bool bSelected = Visuals->IsSelected(H);
		const bool bRouting = bHasBody && Info.RoutState >= static_cast<uint8>(EBattleRoutState::Routing);
		const FLinearColor TeamColor = Visuals->GetTeam(H) == EDemoTeam::Enemy
			? FLinearColor(0.9f, 0.25f, 0.2f)
			: FLinearColor(0.2f, 0.55f, 0.95f);
		const float BodyAlpha = bRouting ? 0.55f : 0.82f;

		// Card body + team bar (+ the selection highlight frame).
		DrawRect(FLinearColor(0.06f, 0.06f, 0.08f, BodyAlpha), Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);
		DrawRect(TeamColor, Rect.Min.X, Rect.Min.Y, 4.f, Rect.GetSize().Y);
		if (bSelected)
		{
			DrawRect(FLinearColor::White, Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, 2.f);
			DrawRect(FLinearColor::White, Rect.Min.X, Rect.Max.Y - 2.f, Rect.GetSize().X, 2.f);
			DrawRect(FLinearColor::White, Rect.Max.X - 2.f, Rect.Min.Y, 2.f, Rect.GetSize().Y);
		}
		if (!bHasBody)
		{
			// Alignment placeholder (roster not spawned yet).
			Canvas->SetDrawColor(120, 120, 124, 160);
			Canvas->DrawText(GEngine->GetMediumFont(), TEXT("..."), Rect.Min.X + 10.f, Rect.Min.Y + 20.f, 0.62f, 0.62f);
			continue;
		}

		const int32 TextAlpha = bRouting ? 120 : 242;
		Canvas->SetDrawColor(235, 235, 235, TextAlpha);
		Canvas->DrawText(GEngine->GetMediumFont(),
			FString::Printf(TEXT("%s%s"),
				GlyphFor(GM ? GM->FindRowStatsForUnit(H) : nullptr), *Info.UnitName),
			Rect.Min.X + 10.f, Rect.Min.Y + 6.f, 0.6f, 0.6f);

		// Alive count x spawn size.
		Canvas->SetDrawColor(165, 215, 165, TextAlpha);
		Canvas->DrawText(GEngine->GetMediumFont(),
			FString::Printf(TEXT("%d/%d"), Info.SoldierCount, FMath::Max(Info.InitialRoster, 1)),
			Rect.Min.X + 10.f, Rect.Min.Y + 24.f, 0.62f, 0.62f);

		// Morale chip (the overlay's state color family) + general star.
		DrawRect(FLinearColor(RoutStateColor(Info.RoutState)), Rect.Max.X - 16.f, Rect.Min.Y + 8.f, 8.f, 8.f);
		if (Info.bGeneral)
		{
			Canvas->SetDrawColor(255, 210, 60, TextAlpha);
			Canvas->DrawText(GEngine->GetMediumFont(), TEXT("*"), Rect.Max.X - 18.f, Rect.Min.Y + 24.f, 0.7f, 0.7f);
		}
	}
	if (PlayerUnits.Num() > DemoCommandBar::MaxCards)
	{
		const FBox2D Rect = DemoCommandBar::CardRect(DemoCommandBar::MaxCards, static_cast<float>(Canvas->SizeY));
		DrawRect(FLinearColor(0.06f, 0.06f, 0.08f, 0.82f), Rect.Min.X, Rect.Min.Y, 44.f, Rect.GetSize().Y);
		Canvas->SetDrawColor(230, 230, 230, 230);
		Canvas->DrawText(GEngine->GetMediumFont(),
			FString::Printf(TEXT("+%d"), PlayerUnits.Num() - DemoCommandBar::MaxCards),
			Rect.Min.X + 10.f, Rect.Min.Y + 20.f, 0.7f, 0.7f);
	}
}

void ADemoHUD::DrawSelectedPanel()
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
	// The panel reads the FIRST selected unit (the M2TW single-unit panel);
	// the stance actions apply to the whole selection (the PC's ApplyStance).
	const FUnitHandle H = Selected[0];
	FDemoUnitBodyInfo Info;
	if (!Visuals->GetUnitBodyInfo(H, Info))
	{
		return;
	}
	ADemoGameMode* GM = GetWorld() ? Cast<ADemoGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	const FBattleRowStats* Stats = GM ? GM->FindRowStatsForUnit(H) : nullptr;
	FUnitMoraleStateView Morale;
	Registry->GetUnitMoraleState(H, Morale);

	const float ViewY = static_cast<float>(Canvas->SizeY);
	const FBox2D Panel = DemoCommandBar::PanelRect(ViewY);
	DrawRect(FLinearColor(0.04f, 0.04f, 0.06f, 0.88f), Panel.Min.X, Panel.Min.Y, Panel.GetSize().X, Panel.GetSize().Y);
	DrawRect(FLinearColor(0.2f, 0.55f, 0.95f, 0.9f), Panel.Min.X, Panel.Min.Y, 3.f, Panel.GetSize().Y);

	Canvas->SetDrawColor(240, 240, 240, 245);
	Canvas->DrawText(GEngine->GetMediumFont(),
		FString::Printf(TEXT("%s  %s  (%s%s)"), *Info.UnitName,
			Stats ? (Stats->bCavalry ? TEXT("Cav") : (Stats->bBow ? TEXT("Missile") : TEXT("Inf"))) : TEXT("--"),
			RoutStateName(Info.RoutState),
			Info.bGarrisoned ? TEXT(", garrisoned") : TEXT("")),
		Panel.Min.X + 10.f, Panel.Min.Y + 6.f, 0.62f, 0.62f);

	// The stat readout: row stats + the live values (the hover card's family).
	FString StatLine = TEXT("ATK --  DEF --  CHG --  ARM --  SHD --");
	if (Stats)
	{
		StatLine = FString::Printf(TEXT("ATK %.0f  DEF %.0f  CHG %.0f  ARM %.0f  SHD %.0f"),
			Stats->MeleeAttack, Stats->MeleeDefense, Stats->ChargeDistance, Stats->Armor, Stats->Shield);
	}
	Canvas->SetDrawColor(200, 200, 205, 235);
	Canvas->DrawText(GEngine->GetMediumFont(), StatLine, Panel.Min.X + 10.f, Panel.Min.Y + 24.f, 0.58f, 0.58f);

	FString LiveLine = FString::Printf(TEXT("x%d/%d  MOR %.2f  FAT %.2f  DISC %s"),
		Info.SoldierCount, FMath::Max(Info.InitialRoster, 1), Info.MoraleValue, Info.FatigueValue,
		DisciplineName(static_cast<uint8>(Morale.Discipline)));
	if (Info.AmmoFraction >= 0.f)
	{
		LiveLine += FString::Printf(TEXT("  AMMO %.0f%%%s"), Info.AmmoFraction * 100.f, Info.bOutOfAmmo ? TEXT(" DRY") : TEXT(""));
	}
	else if (Stats && Stats->RangedRangeM > 0.f)
	{
		LiveLine += FString::Printf(TEXT("  RNG %.0fm"), Stats->RangedRangeM);
	}
	Canvas->SetDrawColor(165, 215, 165, 235);
	Canvas->DrawText(GEngine->GetMediumFont(), LiveLine, Panel.Min.X + 10.f, Panel.Min.Y + 42.f, 0.58f, 0.58f);

	// The stance buttons: read-back state from the getters (the setters are the
	// PC's ApplyStance; the keys G/F/R/X fire the first four). Out-of-ammo
	// grays the fire control (D.15); egress lights only when garrisoned.
	const FLinearColor On(0.35f, 0.8f, 0.35f, 0.95f);
	const FLinearColor Off(0.14f, 0.14f, 0.17f, 0.9f);
	const FLinearColor Warn(0.85f, 0.65f, 0.15f, 0.95f);
	const FLinearColor Gray(0.3f, 0.3f, 0.33f, 0.9f);
	auto Button = [this, ViewY](DemoCommandBar::EAction A, const FString& Label, const FLinearColor& StateFill)
	{
		const FBox2D R = DemoCommandBar::ActionRect(A, ViewY);
		DrawRect(FLinearColor(0.03f, 0.03f, 0.05f, 0.92f), R.Min.X, R.Min.Y, R.GetSize().X, R.GetSize().Y);
		DrawRect(StateFill, R.Min.X, R.Min.Y, R.GetSize().X, 3.f);
		Canvas->SetDrawColor(225, 225, 228, 240);
		Canvas->DrawText(GEngine->GetMediumFont(), Label, R.Min.X + 6.f, R.Min.Y + 5.f, 0.56f, 0.56f);
	};
	Button(DemoCommandBar::EAction::Guard, TEXT("Guard [G]"), Info.bGuardMode ? Warn : Off);
	Button(DemoCommandBar::EAction::FireAtWill,
		Info.bOutOfAmmo ? TEXT("Fire [F] OA") : (Info.bFireAtWill ? TEXT("Fire [F]") : TEXT("Hold [F]")),
		Info.bOutOfAmmo ? Gray : (Info.bFireAtWill ? On : Off));
	Button(DemoCommandBar::EAction::Skirmish, TEXT("Skirm [R]"), Info.bSkirmish ? On : Off);
	Button(DemoCommandBar::EAction::ExitGarrison, TEXT("Egress [X]"), Info.bGarrisoned ? On : Off);
	Button(DemoCommandBar::EAction::Wider, TEXT("Wider"), Off);
	Button(DemoCommandBar::EAction::Narrower, TEXT("Narrow"), Off);
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
		TEXT("G - guard  F - fire-at-will  R - skirmish  X - exit garrison"),
		TEXT("H                        - halt        WASD/arrows - pan"),
		TEXT("Q/E - rotate             - mouse wheel - zoom"),
		TEXT("F3                       - toggle debug overlay (slots/attacks/corpses)"),
	};
	FVector2D Pos(20.f, 20.f);
	Canvas->SetDrawColor(216, 216, 216, 204);
	for (const TCHAR* Line : Lines)
	{
		Canvas->DrawText(GEngine->GetMediumFont(), Line, Pos.X, Pos.Y, 0.8f, 0.8f);
		Pos.Y += 18.f;
	}
}

void ADemoHUD::DrawBeatLabel()
{
	UWorld* World = GetWorld();
	const ADemoGameMode* GM = World ? Cast<ADemoGameMode>(World->GetAuthGameMode()) : nullptr;
	if (!GM || !Canvas || !GEngine || !GEngine->GetMediumFont() || GM->GetBeatIndex() == INDEX_NONE)
	{
		return;
	}
	const FString Text = FString::Printf(TEXT("Beat %d -- %s"),
		GM->GetBeatIndex(), *GM->GetBeatLabel().ToString());
	Canvas->SetDrawColor(255, 224, 64, 235);
	// Top-center, rough half-width offset (the medium font at 1.1 scale).
	Canvas->DrawText(GEngine->GetMediumFont(), Text,
		FMath::Max(4.f, Canvas->SizeX * 0.5f - 140.f), 14.f, 1.1f, 1.1f);
}

// -- PR-2 banners ---------------------------------------------------------------
// The M2TW world layer: one card per unit anchored above the body center,
// collapsed to a small flag when the projected frontage is narrow (zoom LOD),
// re-expanded on hover. The data is the demo's public-API read surface: the
// visual subsystem's body summary (the same struct the PC hit tests use) plus
// the game mode's EDU row-stats cache for the hover card. The morale chip
// mirrors MoraleTickProcessor::DrawMoraleOverlay's state color family (the
// plugin's overlay stays the analytic home; this is the presentation copy).

void ADemoHUD::DrawUnitBanners()
{
	UDemoUnitVisualSubsystem* Visuals = GetWorld() ? GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(GetOwningPlayerController());
	if (!Visuals || !PC || !Canvas || !GEngine || !GEngine->GetMediumFont())
	{
		return;
	}

	// Hover: the banner under the mouse this frame (display only; the click
	// path hit tests the same rects on the controller side).
	HoveredBanner = FUnitHandle();
	float MouseX = 0.f, MouseY = 0.f;
	if (PC->GetMousePosition(MouseX, MouseY))
	{
		Visuals->FindBannerAtScreen(FVector2D(MouseX, MouseY), HoveredBanner);
	}

	for (const FUnitHandle& Handle : Visuals->GetAllUnits())
	{
		FDemoUnitBodyInfo Info;
		if (!Visuals->GetUnitBodyInfo(Handle, Info))
		{
			continue;
		}
		FBox2D Rect;
		bool bCollapsed = false;
		if (!Visuals->GetBannerRect(Handle, Rect, bCollapsed))
		{
			continue;
		}
		const bool bSelected = Visuals->IsSelected(Handle);
		const FLinearColor TeamColor = Info.Team == EDemoTeam::Enemy
			? FLinearColor(0.9f, 0.25f, 0.2f)
			: FLinearColor(0.2f, 0.55f, 0.95f);
		const FColor TeamFColor = Info.Team == EDemoTeam::Enemy ? FColor(230, 64, 51) : FColor(51, 140, 242);

		if (bCollapsed && Handle != HoveredBanner)
		{
			// The small flag: team-colored body + the morale chip at the base.
			DrawRect(FLinearColor(0.05f, 0.05f, 0.07f, 0.75f), Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);
			DrawRect(TeamColor, Rect.Min.X + 2.f, Rect.Min.Y + 2.f, Rect.GetSize().X - 4.f, Rect.GetSize().Y - 10.f);
			DrawRect(FLinearColor(RoutStateColor(Info.RoutState)), Rect.Min.X + 2.f, Rect.Max.Y - 8.f, Rect.GetSize().X - 4.f, 6.f);
			continue;
		}

		// The expanded card: 96 x 30.
		DrawRect(FLinearColor(0.05f, 0.05f, 0.07f, 0.8f), Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);
		DrawRect(TeamColor, Rect.Min.X, Rect.Min.Y, 3.f, Rect.GetSize().Y);
		if (bSelected)
		{
			DrawRect(FLinearColor::White, Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, 1.f);
			DrawRect(FLinearColor::White, Rect.Min.X, Rect.Max.Y - 1.f, Rect.GetSize().X, 1.f);
			DrawRect(FLinearColor::White, Rect.Max.X - 1.f, Rect.Min.Y, 1.f, Rect.GetSize().Y);
		}

		ADemoGameMode* GM = GetWorld() ? Cast<ADemoGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
		const FBattleRowStats* Stats = GM ? GM->FindRowStatsForUnit(Handle) : nullptr;
		Canvas->SetDrawColor(235, 235, 235, 235);
		Canvas->DrawText(GEngine->GetMediumFont(), GlyphFor(Stats), Rect.Min.X + 6.f, Rect.Min.Y + 3.f, 0.55f, 0.55f);
		Canvas->DrawText(GEngine->GetMediumFont(), Info.UnitName, Rect.Min.X + 22.f, Rect.Min.Y + 3.f, 0.58f, 0.58f);

		// Soldier bar (live roster vs the spawn size).
		const float Denom = FMath::Max(1, Info.InitialRoster);
		const float Frac = FMath::Clamp(static_cast<float>(Info.SoldierCount) / Denom, 0.f, 1.f);
		DrawRect(FLinearColor(0.15f, 0.15f, 0.17f, 0.9f), Rect.Min.X + 5.f, Rect.Min.Y + 20.f, 54.f, 6.f);
		DrawRect(FLinearColor(0.3f, 0.85f, 0.35f, 0.95f), Rect.Min.X + 5.f, Rect.Min.Y + 20.f, 54.f * Frac, 6.f);
		Canvas->SetDrawColor(220, 220, 220, 220);
		Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("%d"), Info.SoldierCount),
			Rect.Min.X + 62.f, Rect.Min.Y + 18.f, 0.5f, 0.5f);

		// Morale chip (the overlay's state color family) + general star.
		const FColor Chip = RoutStateColor(Info.RoutState);
		DrawRect(FLinearColor(Chip), Rect.Min.X + 80.f, Rect.Min.Y + 4.f, 8.f, 8.f);
		if (Info.bGeneral)
		{
			// A four-point star, gold -- the general's mark.
			const float Cx = Rect.Min.X + 90.5f, Cy = Rect.Min.Y + 8.f;
			DrawLine(Cx, Cy - 5.f, Cx, Cy + 5.f, FColor(255, 210, 60), 2.f);
			DrawLine(Cx - 5.f, Cy, Cx + 5.f, Cy, FColor(255, 210, 60), 2.f);
		}

		// Ammo pips for missile rows (4 pips; "OA" when dry). Melee rows show
		// nothing -- the glyph carries the type.
		if (Info.AmmoFraction >= 0.f)
		{
			if (Info.bOutOfAmmo)
			{
				Canvas->SetDrawColor(240, 70, 50, 235);
				Canvas->DrawText(GEngine->GetMediumFont(), TEXT("OA"), Rect.Min.X + 66.f, Rect.Min.Y + 3.f, 0.5f, 0.5f);
			}
			else
			{
				const int32 Pips = FMath::Clamp(FMath::CeilToInt(Info.AmmoFraction * 4.f), 0, 4);
				for (int32 p = 0; p < 4; ++p)
				{
					DrawRect(p < Pips ? FLinearColor(0.95f, 0.8f, 0.3f, 0.95f) : FLinearColor(0.25f, 0.25f, 0.28f, 0.9f),
						Rect.Min.X + 66.f + p * 6.f, Rect.Min.Y + 20.f, 4.f, 6.f);
				}
			}
		}

		// Toggle badges, right-aligned: G = guard on, H = fire hold, S = skirmish.
		float BadgeX = Rect.Max.X - 6.f;
		Canvas->SetDrawColor(150, 200, 255, 235);
		if (Info.bSkirmish)
		{
			Canvas->DrawText(GEngine->GetMediumFont(), TEXT("S"), BadgeX -= 8.f, Rect.Min.Y + 18.f, 0.5f, 0.5f);
		}
		if (!Info.bFireAtWill)
		{
			Canvas->SetDrawColor(240, 70, 50, 235);
			Canvas->DrawText(GEngine->GetMediumFont(), TEXT("H"), BadgeX -= 8.f, Rect.Min.Y + 18.f, 0.5f, 0.5f);
		}
		if (Info.bGuardMode)
		{
			Canvas->SetDrawColor(255, 210, 60, 235);
			Canvas->DrawText(GEngine->GetMediumFont(), TEXT("G"), BadgeX -= 8.f, Rect.Min.Y + 18.f, 0.5f, 0.5f);
		}
	}

	// The hover stats card (after all banners: draws on top).
	if (HoveredBanner.IsValid())
	{
		FBox2D Rect;
		bool bCollapsedDummy = false;
		if (Visuals->GetBannerRect(HoveredBanner, Rect, bCollapsedDummy))
		{
			DrawHoverCard(HoveredBanner, Rect);
		}
	}
}

void ADemoHUD::DrawHoverCard(FUnitHandle Handle, const FBox2D& BannerRect)
{
	UDemoUnitVisualSubsystem* Visuals = GetWorld() ? GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
	ADemoGameMode* GM = GetWorld() ? Cast<ADemoGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Visuals || !Registry || !Canvas || !GEngine || !GEngine->GetMediumFont())
	{
		return;
	}
	FDemoUnitBodyInfo Info;
	if (!Visuals->GetUnitBodyInfo(Handle, Info))
	{
		return;
	}
	const FBattleRowStats* Stats = GM ? GM->FindRowStatsForUnit(Handle) : nullptr;

	// Below the banner, clamped into the viewport.
	constexpr float W = 232.f, H = 88.f;
	float X = BannerRect.Min.X;
	float Y = BannerRect.Max.Y + 4.f;
	const float vw = static_cast<float>(Canvas->SizeX), vh = static_cast<float>(Canvas->SizeY);
	X = FMath::Clamp(X, 4.f, FMath::Max(4.f, vw - W - 4.f));
	Y = FMath::Clamp(Y, 4.f, FMath::Max(4.f, vh - H - 4.f));

	DrawRect(FLinearColor(0.04f, 0.04f, 0.06f, 0.88f), X, Y, W, H);
	DrawRect(FLinearColor(0.2f, 0.55f, 0.95f, 0.9f), X, Y, 3.f, H);

	FLinearColor TeamColor = Info.Team == EDemoTeam::Enemy
		? FLinearColor(0.9f, 0.25f, 0.2f)
		: FLinearColor(0.2f, 0.55f, 0.95f);
	DrawRect(TeamColor, X + 8.f, Y + 6.f, 6.f, 6.f);
	Canvas->SetDrawColor(240, 240, 240, 245);
	Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("%s  (%s)"),
		*Info.UnitName, RoutStateName(Info.RoutState)), X + 20.f, Y + 4.f, 0.6f, 0.6f);

	const TCHAR* TypeName = Stats
		? (Stats->bCavalry ? TEXT("Cavalry") : (Stats->bBow ? TEXT("Missile infantry") : TEXT("Melee infantry")))
		: TEXT("--");
	Canvas->SetDrawColor(200, 200, 205, 235);
	Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("%s   x%d/%d"),
		TypeName, Info.SoldierCount, Info.InitialRoster), X + 8.f, Y + 22.f, 0.55f, 0.55f);

	FString StatLine = TEXT("ATK --  DEF --  CHG --  ARM --  SHD --");
	if (Stats)
	{
		StatLine = FString::Printf(TEXT("ATK %.0f  DEF %.0f  CHG %.0f  ARM %.0f  SHD %.0f"),
			Stats->MeleeAttack, Stats->MeleeDefense, Stats->ChargeDistance, Stats->Armor, Stats->Shield);
	}
	Canvas->DrawText(GEngine->GetMediumFont(), StatLine, X + 8.f, Y + 38.f, 0.55f, 0.55f);

	FString LiveLine = FString::Printf(TEXT("MOR %.2f  FAT %.2f"), Info.MoraleValue, Info.FatigueValue);
	if (Info.AmmoFraction >= 0.f)
	{
		LiveLine += FString::Printf(TEXT("  AMMO %.0f%%%s"), Info.AmmoFraction * 100.f, Info.bOutOfAmmo ? TEXT(" DRY") : TEXT(""));
	}
	else if (Stats)
	{
		LiveLine += FString::Printf(TEXT("  RNG %.0fm"), Stats->RangedRangeM);
	}
	Canvas->SetDrawColor(165, 215, 165, 235);
	Canvas->DrawText(GEngine->GetMediumFont(), LiveLine, X + 8.f, Y + 54.f, 0.55f, 0.55f);

	const TCHAR* OrderName = TEXT("--");
	if (const FUnitOrder* Order = Registry->FindOrder(Handle))
	{
		switch (Order->Type)
		{
		case FUnitOrder::EType::Move: OrderName = TEXT("Move"); break;
		case FUnitOrder::EType::Stop: OrderName = TEXT("Halt"); break;
		case FUnitOrder::EType::Garrison: OrderName = TEXT("Garrison"); break;
		case FUnitOrder::EType::ExitGarrison: OrderName = TEXT("Egress"); break;
		case FUnitOrder::EType::AttackMove: OrderName = TEXT("Attack-move"); break;
		case FUnitOrder::EType::AttackUnit: OrderName = TEXT("Attack"); break;
		default: break;
		}
	}
	Canvas->SetDrawColor(255, 224, 64, 235);
	Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("Order: %s   G:%s F:%s S:%s"),
		OrderName,
		Info.bGuardMode ? TEXT("on") : TEXT("off"),
		Info.bFireAtWill ? TEXT("on") : TEXT("HOLD"),
		Info.bSkirmish ? TEXT("on") : TEXT("off")), X + 8.f, Y + 70.f, 0.55f, 0.55f);
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
		// Ring sized to the unit's actual frontage, drawn on the visual blob
		// (the trimmed entity centroid) -- the card and the ring agree.
		DrawWorldCircle(Info.VisualCentroidCm, RadiusCm, FColor::Green);
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
