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
