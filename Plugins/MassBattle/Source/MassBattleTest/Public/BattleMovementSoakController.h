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
	// Audit M16: the gate carries a generous DURING-soak budget (disclosed)
	// next to the settled final state -- the old shape manufactured its own
	// settle and gated nothing but tautologies.
	int32 FieldWideOrders = 0;
	// Audit M16: the STALL signal is persistence, not churn -- entities at
	// ladder level >= 3 in a sample, tracked as the worst sample. A jammed
	// formation parks entities at deep levels sample after sample; contact
	// churn spikes recover between samples.
	int32 MaxDuringStuckDeep = 0;
	// Audit L14: units skipped by the final assert (roster/formation lookup
	// failed) must be visible -- a unit whose fragment vanished escapes the
	// stuck/separation accounting otherwise.
	int32 SkippedUnitsAtAssert = 0;

	// Phase 2 D2: worst sampled MaxSlotErrorM across the soak (the dress
	// metric; sampled with the ~5 s sampler, logged in the FINAL line and
	// bounded when a phase-2 movement flag is on).
	float MaxDressErrorM = 0.f;

	// Phase 2 task 9 (R-C9): the clash-mix order count (logged in the FINAL
	// line; 0 when Battle.Combat.SoakClash is off).
	int32 ClashOrders = 0;

	// Effective knobs (CVAR-overridable; defaults preserve the T1.8
	// baseline byte-for-byte).
	int32 UnitsPerRow = 4;          // Battle.Movement.SoakGrid x units per row (grid = N x N/2... 4x2 = 8)
	int32 UnitRows = 2;             // Battle.Movement.SoakRows
	float SoakDuration = 600.f;     // Battle.Movement.SoakDuration

	static constexpr float OrderIntervalSeconds = 10.f;
	static constexpr float SampleIntervalSeconds = 5.f;
};
