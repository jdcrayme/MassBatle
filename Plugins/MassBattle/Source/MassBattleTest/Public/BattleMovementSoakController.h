// Phase 1 / Task 12 (gate T1.8): Gauntlet movement mini-soak.
// -gauntlet=BattleMovementSoak  --  8 units, 10 minutes (game time) of seeded
// random orders issued through the A2 registry order API. Asserts (roadmap
// T1.8 verbatim): stuck = 0 (unstuck-ladder escalations), separation = 0
// (no entity outside its unit body / co-located pairs), archetype count
// constant. Applies the test-mode contract at the staged -game boot (A4 guard
// rail: fixed step + the contract cvars must hold or the soak is vacuous).
// Evidence lines mirror the GAUNTLET convention from the phase-0 smoke.
#pragma once

#include "CoreMinimal.h"
#include "GauntletTestController.h"
#include "BattleMovementSoakController.generated.h"

UCLASS()
class UBattleMovementSoakController : public UGauntletTestController
{
	GENERATED_BODY()

public:
	virtual void OnInit() override;
	virtual void OnTick(float TimeDelta) override;
	virtual ~UBattleMovementSoakController() override;

private:
	void FinishTest(int32 ExitCode);

	enum class EPhase : uint8 { WaitForWorld, Spawn, Soak, FinalSettle, Assert, Done };

	EPhase Phase = EPhase::WaitForWorld;
	float PhaseTime = 0.f;
	float SoakTime = 0.f;
	float NextOrderTime = 0.f;
	float NextSampleTime = 0.f;
	int32 SpawnTicks = 0;
	int32 ArchetypeCountAtSpawn = 0;
	int32 EscalationsAtSpawn = 0;
	int32 SeparationViolations = 0;
	int32 Samples = 0;
	int32 SoakEscalations = 0;
	int32 SettleTicks = 0;
	bool bTestModeOk = false;

	static constexpr float SoakDurationSeconds = 600.f;
	static constexpr float OrderIntervalSeconds = 10.f;
	static constexpr float SampleIntervalSeconds = 5.f;
};
