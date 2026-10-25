#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.DeathPath.* -- death up-path unit-level probes.
// TagRegistered (audit H1): "Battle.Unit.Died" must exist as a REGISTERED
// gameplay tag in every configuration. The pre-fix implementation requested it
// with ErrorIfNotFound=false, so an unregistered tag silently produced an
// empty filter container -- and empty containers match ALL gameplay events
// (engine-verified), which made the T0.5 GAS count pass while the tag itself
// did not exist.
#include "Misc/AutomationTest.h"

#include "BattleGameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeathPath_TagRegistered,
	"Battle.Unit.DeathPath.TagRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeathPath_TagRegistered::RunTest(const FString& Parameters)
{
	const FGameplayTag& Native = TAG_Battle_Unit_Died.GetTag();
	TestTrue(TEXT("native tag is valid"), Native.IsValid());
	TestEqual(TEXT("native tag name"), Native.GetTagName(), FName(TEXT("Battle.Unit.Died")));

	// Registered in the global tree: a fresh lookup (the way external GAS
	// consumers find it) must resolve, not return the ErrorIfNotFound=false
	// invalid-tag fallback.
	const FGameplayTag LookedUp = FGameplayTag::RequestGameplayTag(FName(TEXT("Battle.Unit.Died")), /*ErrorIfNotFound*/ false);
	TestTrue(TEXT("tag resolves from the registry tree"), LookedUp == Native);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
