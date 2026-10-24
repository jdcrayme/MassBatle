using UnrealBuildTool;

public class MassBattle : ModuleRules
{
	public MassBattle(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			// Mass stack (5.8): core + spawner + signals + common movement/transforms
			"MassEntity",
			"MassCore",      // 5.8: base fragment/tag structs live here (audit C7); explicit link needed (reality fix)
			"MassSpawner",   // UMassSpawnerSubsystem (module under MassGameplay plugin)
			"MassSignals",   // core engine module in 5.8
			"MassCommon",
			"MassSimulation",  // Task 8: UMassSimulationSubsystem probe + Mass phases in game worlds
			"MassLOD",         // audit M2: FMassVariableTickChunkFragment probe in the T0.9 contract assert
			// GAS arrives with Phase 0 unit actors; linking now avoids a config churn later
			"GameplayAbilities",
			// Task 9: FGameplayTag / FGameplayTagContainer (death up-path events)
			"GameplayTags",
			// Task 1 (R8): Gauntlet smoke controller (engine plugin; Experimental in 5.8)
			"Gauntlet",
			// Task 8: AFunctionalTest S-scenarios (Developer module; Development
			// game builds only -- the func tier is a dev-build tier per B.0)
			"FunctionalTesting"
			// Future (add at the phase that needs them — see arch §3/§4):
			// "MassSimulation", "MassActors", "MassLOD", "MassRepresentation",
			// "MassNavigation", "MassAvoidance", "MassMovement", "MassAIBehavior", "MassStateTree"
		});
	}
}
