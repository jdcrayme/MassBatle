// DemoGameMode.h -- demo game mode: wires the demo classes and discovers
// level-placed ADemoUnitActors, spawning their rosters through the plugin's
// batched spawn path once the world is safe (game thread, Mass not
// processing). All scenario content (ground, lights, units) is authored in
// the map; nothing is spawned procedurally.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DemoTypes.h"
#include "BattleScenarioAsset.h"
#include "DemoGameMode.generated.h"

class ADemoUnitActor;
class UBattleArrowVisualComponent;

UCLASS()
class MASSBATTLEGAME_API ADemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADemoGameMode();

	// -- AGameModeBase --
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** PR-1 (demo-shell spec section 3): spawns the spec section 2 field
	 *  (4 garrison structures + 12 dictionary-keyed units, generals stamped)
	 *  and starts the beat sequencer. The Demo.SpawnField command + the
	 *  demo-map no-level-units fallback both land here (the M3/M4/M5 chain
	 *  stays console-only CI evidence). Public: the console command path. */
	void SpawnFieldFixture();

	/** PR-1: the current beat's HUD surface (INDEX_NONE beat = idle). */
	const FName& GetBeatLabel() const { return BeatLabel; }
	int32 GetBeatIndex() const { return CurrentBeat; }

	/** PR-1 shoring: the delayed real-attack repro (the owner report
	 *  2026-10-31: RMB attack walks to the enemy and never engages). Fires
	 *  IssueAttackUnitOrder after a delay, then polls engagement telemetry
	 *  (head-entity anchor + cached fatigue) so the logs carry the answer.
	 *  Public: the Demo.AttackTest command path. */
	void AttackTestFire(int32 UnitIdx, int32 TargetIdx);
	void AttackTestPoll(int32 UnitIdx, int32 TargetIdx, int32 Poll);

private:
	/** Deferred spawn pump: retries until the world + Mass are ready. */
	void TrySpawnLevelUnits();
	/** PR-1: spawns the map-assigned scenario asset's rows (data-driven
	 *  composition; the M7 field fixture below is the empty-asset fallback). */
	void SpawnScenarioAsset();
	void AdoptVisuals();

	// -- PR-1: the beat sequencer + the M7 field fixture (demo-shell spec
	// section 2/3). The field table below is the code-side half of the demo
	// field; the map-placed ADemoUnitActor path takes precedence once the
	// owner's editor pass lands (the PR-11 posture). Beats 0-3 are the PR-1
	// skeleton; the morale/garrison beats 4-7 are the PR-4 carve-out. --
	struct FDemoFieldUnit
	{
		const TCHAR* Dictionary;
		int32 Count;
		int32 NumRanks;
		FVector2D PosM;
		float YawDeg;
		uint8 TeamId;
		int32 OfficerCount;
		bool bIsGeneral;
		EDemoTeam Team;
		FName Role;
	};
	struct FDemoBeat
	{
		float AtSeconds;
		FName Label;
	};

	static const FDemoBeat* GetBeatTable(int32& OutCount);
	/** The spec demo-shell section 2.1 order of battle, ONE home for the
	 *  fixture spawn and the level-unit binding (PR-4 early slice). */
	static const FDemoFieldUnit* GetFieldTable(int32& OutCount);
	void TickSequencer(float DeltaSeconds);
	void FireBeat(int32 BeatIndex);
	/** PR-4 (early slice): binds the beat sequencer to the LEVEL-PLACED
	 *  units -- role = the field-table row whose dictionary + spawn position
	 *  (within 5 m) match the placed actor. Logs EXEC_RESULT DemoBind. */
	void BindSequencerToLevelUnits();

	/** PR-1: the authored scenario (spawn rows + auto order) assigned in the
	 *  map. Empty keeps the M7 field fixture fallback. */
	UPROPERTY(EditAnywhere, Category = "Battle|Demo")
	TObjectPtr<UDemoScenarioAsset> ScenarioAsset;

	/** True once the scenario asset spawned (or none was assigned). */
	bool bScenarioSpawned = false;

	// -- PR-1 sequencer state --
	/** The current beat's HUD label (None = no sequencer running). */
	UPROPERTY(Transient) FName BeatLabel;
	int32 CurrentBeat = INDEX_NONE;
	float SequencerTime = 0.f;
	bool bSequencerRunning = false;
	bool bFieldSpawned = false;
	/** Role -> live handle (the sequencer commands by role, P1..E5). */
	TMap<FName, FUnitHandle> FieldUnitByRole;
	/** Spawned handle -> its level actor (the level-path binding key). */
	TMap<FUnitHandle, TObjectPtr<ADemoUnitActor>> HandleByActor;
	/** Demo.AttackTest poll timers (15 x 2 s engagement telemetry). */
	FTimerHandle PollHandles[15];
	/** The demo-session straggler-pace override's shipped value (restored in
	 *  EndPlay; the func-tier travel-leak discipline). */
	int32 SavedStragglerPace = 1;

	/** One-time click-path probe after the level settles: resolves each
	 *  registered structure's own footprint center through the SAME static
	 *  the RMB click uses -- EXEC_RESULT DemoClickProbe evidence that
	 *  map-placed structures are clickable. */
	void VerifyGarrisonStructures();

	/** Grace-tick counter for the deferred spawn pump. */
	int32 SpawnGraceTicks = 0;

	/** True once every discovered unit actor has spawned its roster. */
	bool bLevelUnitsSpawned = false;

	/** Level-placed unit actors found at BeginPlay (authored in the map). */
	UPROPERTY() TArray<TObjectPtr<ADemoUnitActor>> LevelUnits;

	/** PR-8: the gray-box arrow feed (one ISM batched from the projectile
	 *  pool), attached when the ranged flags are on. */
	UPROPERTY(Transient) TObjectPtr<UBattleArrowVisualComponent> ArrowVisuals;
};
