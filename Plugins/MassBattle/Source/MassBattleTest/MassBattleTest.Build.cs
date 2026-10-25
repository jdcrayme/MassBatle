// MassBattleTest.Build.cs -- Developer-type test module (phase-1 Task 13,
// audit-flagged A4 split): all automation tests, S-scenario actors, the func
// harness, and the Gauntlet controllers live here, so FunctionalTesting /
// Gauntlet deps never touch the Runtime MassBattle module (Shipping link fix).
// Developer modules compile into Development Editor/Game targets and are
// stripped from Shipping -- the nightly staged Development build keeps the
// controllers (the engine's own Gauntlet controllers ship the same way).
using UnrealBuildTool;

public class MassBattleTest : ModuleRules
{
	public MassBattleTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			// Test surface exported from the runtime module (registry, spawn,
			// fragments, math seam, subsystems, debug counters).
			"MassBattle"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			// Mass stack surfaces the tests probe/assert against.
			"MassEntity",
			"MassSpawner",
			"MassCore",
			"MassCommon",
			"MassMovement",
			"MassSimulation",
			"MassLOD",
			"GameplayAbilities",
			"GameplayTags",
			// Task 8: AFunctionalTest S-scenarios (Developer module).
			"FunctionalTesting",
			// R8: Gauntlet controllers (nightly smoke + T1.8 soak).
			"Gauntlet"
		});
	}
}
