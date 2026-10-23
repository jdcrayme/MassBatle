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
			"MassSpawner",   // UMassSpawnerSubsystem (module under MassGameplay plugin)
			"MassSignals",   // core engine module in 5.8
			"MassCommon",
			// GAS arrives with Phase 0 unit actors; linking now avoids a config churn later
			"GameplayAbilities"
			// Future (add at the phase that needs them — see arch §3/§4):
			// "MassSimulation", "MassActors", "MassLOD", "MassRepresentation",
			// "MassNavigation", "MassAvoidance", "MassMovement", "MassAIBehavior", "MassStateTree"
		});
	}
}
