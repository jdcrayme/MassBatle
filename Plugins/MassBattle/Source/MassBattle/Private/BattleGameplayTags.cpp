// BattleGameplayTags.cpp -- native tag definitions (see BattleGameplayTags.h).
#include "BattleGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Battle_Unit_Died,
	"Battle.Unit.Died",
	"Death up-path event raised on the unit actor's ASC when one of its entities dies (arch 5.2). Native so it resolves in editor, -game, and packaged builds (audit H1).")

UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Battle_Unit_OfficerDied,
	"Battle.Unit.OfficerDied",
	"Phase 4a officer-death event on the owning unit actor's ASC (spec morale-and-gas 4.7): the dying entity carried FBaseStatsFragment.bOfficer. Native (the H1 lesson).")

UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Battle_Team_GeneralDied,
	"Battle.Team.GeneralDied",
	"Phase 4a general-death event raised on every same-team unit actor's ASC (spec morale-and-gas 4.7): the team's general entity died; the registry stamps the team shock + invalidates the aura record. Native (the H1 lesson).")
