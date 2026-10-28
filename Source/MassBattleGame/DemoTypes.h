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
 *  HUD drawing and formation-drag math. */
struct FDemoUnitBodyInfo
{
	FUnitHandle Handle;
	EDemoTeam Team = EDemoTeam::Player;
	/** Body (formation block) center, cm, planar. */
	FVector2D BodyCenterCm = FVector2D::ZeroVector;
	float FacingYawDeg = 0.f;
	int32 NumFiles = 0;
	int32 NumRanks = 0;
	/** Formation spacings (meters). */
	float FileSpacingM = 1.2f;
	float RankSpacingM = 1.2f;
	int32 SoldierCount = 0;
	FString UnitName;
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
