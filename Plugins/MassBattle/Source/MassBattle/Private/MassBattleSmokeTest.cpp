// Pre-phase-0 scaffold proof: the automation harness works end to end.
// Phase 0 replaces this file's namespace siblings with Battle.Unit / Battle.Data / Battle.Func suites (roadmap B.0).
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMassBattleSmoke_PluginLoaded,
	"Battle.Smoke.PluginLoaded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FMassBattleSmoke_PluginLoaded::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("MassBattle runtime module is loaded in the editor process"),
		FModuleManager::Get().IsModuleLoaded("MassBattle"));
	return true;
}
