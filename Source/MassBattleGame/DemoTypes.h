// DemoTypes.h -- shared plain data types of the MassBattle demo (main-project
// module). The demo consumes the plugin's registry API and reads fragments;
// it owns no simulation state. Positions in centimeters (fragment boundary
// convention), spacings in meters (project convention).
#pragma once

#include "CoreMinimal.h"
#include "BattleTypes.h"

/** Demo-side faction. The registry is faction-agnostic (FUnitHandle identity
 *  only); selection and tinting need a player/enemy split, which lives here. */
UENUM(BlueprintType)
enum class EDemoTeam : uint8
{
	Player,
	Enemy
};

/** Unit body summary read from the head entity's FUnitFormationSharedFragment
 *  (same derivation as the soak controller / T1.7 asserts). Used by picking,
 *  HUD drawing and formation-drag math. PR-2 banner fields appended: the
 *  always-visible card + the hover stats read the SAME struct. */
struct FDemoUnitBodyInfo
{
	FUnitHandle Handle;
	EDemoTeam Team = EDemoTeam::Player;
	/** Body (formation block) center, cm, planar -- the SIM's formation anchor
	 *  view (the shared fragment's anchor, rank-depth adjusted). The order
	 *  math and the sim read this one. */
	FVector2D BodyCenterCm = FVector2D::ZeroVector;
	/** PR-2 shoring (the owner report: entities outrun the banner): the
	 *  trimmed centroid of the LIVING entities (mean, drop the farthest
	 *  quarter, re-mean -- stragglers don't pull it). The banner/cards track
	 *  this one. Equal to BodyCenterCm until the first visual tick. */
	FVector2D VisualCentroidCm = FVector2D::ZeroVector;
	float FacingYawDeg = 0.f;
	int32 NumFiles = 0;
	int32 NumRanks = 0;
	/** Formation spacings (meters). */
	float FileSpacingM = 1.2f;
	float RankSpacingM = 1.2f;
	int32 SoldierCount = 0;
	FString UnitName;

	// -- PR-2 banner fields (all demo-module reads through the public API) --
	/** EBattleRoutState (the morale chip's color index). */
	uint8 RoutState = 0;
	float MoraleValue = 0.f;
	float FatigueValue = 0.f;
	/** Ammo pool fraction; < 0 = the unit is not missile-capable. */
	float AmmoFraction = -1.f;
	/** Roster at spawn (soldiers + officers) -- the soldier bar's denominator. */
	int32 InitialRoster = 0;
	bool bGeneral = false;
	bool bGarrisoned = false;
	bool bGuardMode = false;
	bool bFireAtWill = true;
	bool bSkirmish = false;
	bool bOutOfAmmo = false;
};

namespace DemoUi
{
	/** Z height (cm) used when projecting a unit's planar BodyCenterCm to
	 *  screen for picking/marquee/labels. The bodies are flat; this is the
	 *  camera-facing elevation the demo art assumes (1 m). ONE home (B-8) --
	 *  every projection call site reads it. */
	constexpr float BodyLabelZCm = 100.f;
}

/** PR-3 command bar (demo-shell spec section 4.3): the screen-space geometry
 *  home for the army card row + the selected panel -- ONE home so the HUD's
 *  draw and the player controller's click hit tests agree by construction
 *  (the PR-2 banner-rect posture, applied to fixed UI). All coordinates are
 *  viewport pixels; callers pass their own viewport height. */
namespace DemoCommandBar
{
	/** The army card row (bottom-left): one card per PLAYER unit, regardless
	 *  of selection -- the M2TW convention. Selected cards highlight. */
	constexpr float CardW = 130.f;
	constexpr float CardH = 58.f;
	constexpr float Gap = 6.f;
	constexpr int32 MaxCards = 10;
	constexpr float LeftMarginPx = 14.f;
	constexpr float BottomMarginPx = 14.f;

	inline float CardRowY(float ViewY) { return ViewY - CardH - BottomMarginPx; }
	inline FBox2D CardRect(int32 Index, float ViewY)
	{
		const float X = LeftMarginPx + Index * (CardW + Gap);
		const float Y = CardRowY(ViewY);
		return FBox2D(FVector2D(X, Y), FVector2D(X + CardW, Y + CardH));
	}

	/** The stance buttons (spec section 4.3 selected panel): each a one-line
	 *  call to the setter family; the keys G/F/R/X fire the first four. */
	enum class EAction : uint8
	{
		Guard, FireAtWill, Skirmish, ExitGarrison, Wider, Narrower, Count
	};

	/** The selected panel (above the card row; drawn iff a selection exists --
	 *  hit tests pass bPanelVisible to match). */
	constexpr float PanelW = 470.f;
	constexpr float PanelH = 96.f;
	inline FBox2D PanelRect(float ViewY)
	{
		const float Y = CardRowY(ViewY) - PanelH - 8.f;
		return FBox2D(FVector2D(LeftMarginPx, Y), FVector2D(LeftMarginPx + PanelW, Y + PanelH));
	}

	constexpr float ButtonW = 68.f;
	constexpr float ButtonH = 18.f;
	inline FBox2D ActionRect(EAction Action, float ViewY)
	{
		const FBox2D Panel = PanelRect(ViewY);
		const float X = Panel.Min.X + 8.f + static_cast<float>(static_cast<uint8>(Action)) * (ButtonW + 6.f);
		const float Y = Panel.Max.Y - ButtonH - 8.f;
		return FBox2D(FVector2D(X, Y), FVector2D(X + ButtonW, Y + ButtonH));
	}
};

/** PR-5 minimap (demo-shell spec section 4.3, the optional-last command-bar
 *  item): a display-only tactical map -- structure footprints, unit dots,
 *  the camera frustum. One geometry home for the HUD's draw (no minimap
 *  clicks in scope). The map frame is the cost-grid tile: callers pass the
 *  plugin's BattleGridGeo::TileHalfExtentM (one home for the extent). */
namespace DemoMinimap
{
	constexpr float SizePx = 180.f;
	constexpr float MarginPx = 14.f;

	inline FBox2D PanelRect(float ViewX, float ViewY)
	{
		const float X = ViewX - SizePx - MarginPx;
		const float Y = ViewY - SizePx - MarginPx;
		return FBox2D(FVector2D(X, Y), FVector2D(X + SizePx, Y + SizePx));
	}

	/** World (cm, planar) -> minimap px inside the panel. Planar x maps to
	 *  screen x, planar y to screen y (north up -- the camera's default). */
	inline FVector2D WorldToMap(const FVector2D& PosCm, float ViewX, float ViewY, float HalfExtentM)
	{
		const FBox2D Panel = PanelRect(ViewX, ViewY);
		const float HalfCm = HalfExtentM * 100.f;
		const float Nx = FMath::Clamp(PosCm.X / (2.f * HalfCm) + 0.5f, 0.f, 1.f);
		const float Ny = FMath::Clamp(0.5f - PosCm.Y / (2.f * HalfCm), 0.f, 1.f); // +y (north) at the top
		return FVector2D(Panel.Min.X + Nx * SizePx, Panel.Min.Y + Ny * SizePx);
	}
};

/** One selected unit's piece of a formation-drag preview/commit: the drag line
 *  divided across units (Total War frontage semantics). */
struct FDemoDragSegment
{
	FUnitHandle Handle;
	/** Front-rank center on the drag line (cm) -- the order anchor. */
	FVector2D AnchorCm = FVector2D::ZeroVector;
	float FacingYawDeg = 0.f;
	int32 Files = 0;
	int32 NumRanks = 0;
};

DECLARE_LOG_CATEGORY_EXTERN(LogDemoBattle, Display, Display);
