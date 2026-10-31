// BattleGameplayTags.h -- native gameplay tags of the battle up-path (arch §5.2).
// Native registration (not ini) so the tags exist in EVERY configuration --
// editor, -game, and packaged builds. Ini-listed tags are editor-only and
// would leave -game/packaged consumers with invalid tags (audit H1: the T0.5
// GAS event only fired because an empty tag-container filter matches ALL
// events).
#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/** "Battle.Unit.Died" -- raised on a unit actor's ASC when one of its entities
 *  dies (death up-path completion, arch §5.2). Consumed via
 *  AddGameplayEventTagContainerDelegate / generic GAS event listeners. */
MASSBATTLE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Battle_Unit_Died)

/** "Battle.Unit.OfficerDied" -- Phase 4a (spec morale-and-gas §4.7, MC8):
 *  raised on the OWNING unit actor's ASC when the dying entity was one of its
 *  officers (the amplified fresh-casualty spike rides the same branch). */
MASSBATTLE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Battle_Unit_OfficerDied)

/** "Battle.Team.GeneralDied" -- Phase 4a (spec morale-and-gas §4.7, MC8):
 *  raised on EVERY same-team unit actor's ASC when a team's general entity
 *  dies (team shock term + aura removal ride the registry side). */
MASSBATTLE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Battle_Team_GeneralDied)
