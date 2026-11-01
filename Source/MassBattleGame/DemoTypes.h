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
