// BattleGameplayTags.cpp -- native tag definitions (see BattleGameplayTags.h).
#include "BattleGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Battle_Unit_Died,
	"Battle.Unit.Died",
	"Death up-path event raised on the unit actor's ASC when one of its entities dies (arch 5.2). Native so it resolves in editor, -game, and packaged builds (audit H1).")
