// BattleMoraleEffects.h -- the MoraleTick gameplay effect + its exec calc +
// the FatigueState effect catalog (PR-12; spec morale-and-gas sections 4.8-
// 4.10, A2/ADR-33, A8). C++-authored GEs, no .uassets (the project's A5
// posture). All engine symbols verified against pinned 5.8.4 source in Task
// 3 (engine-api Part A section 16 / Part B section 4.14):
//
// - The MoraleTick GE: infinite duration, one execution definition carrying
//   UBattleMoraleExecCalc. The application sets the SPEC's Period per mode
//   (the def's Period is a placeholder): under Battle.Morale.DeterministicTick=1
//   the spec applies with Period = NO_PERIOD, so the natural repeating
//   FTimerManager timer is NEVER created (GameplayEffect.cpp:4497 gate) and
//   the morale pass's forced ExecutePeriodicEffect calls are the ONLY
//   executions -- the double-fire window closes by construction (the Task 3
//   R-M2 verdict). Shipping mode applies Period = Battle.Morale.TickPeriod
//   (the natural wall-clock timer; T4.8 bounds the staleness).
// - The exec calc: pulls the sim-owned FMoraleTickInputs block + the exertion
//   integral from the registry through the TARGET ASC's owner actor, calls
//   BattleMorale::EvaluateMorale (ONE home -- A14: the pass never evaluates
//   the stack), writes Morale + Fatigue through OutExecutionOutput as
//   OVERRIDE mods (each tick SETS the absolute value; Additive would
//   accumulate per period). It never reads wall-clock, never iterates
//   entities, never issues orders (ground rule 2). The one registry write it
//   makes is the staleness stamp (LastMoraleTickSimTime = world game
//   seconds -- sim time, not wall-clock; pipeline state, excluded from the
//   hash per A10; the T4.8 age bound reads it).
// - The fatigue catalog: six infinite GEs whose ctors build five ADDITIVE
//   modifiers on the *Mod attributes at GetFatigueMultipliers(state) - 1
//   (ADR-09: buffs/debuffs target *Mod only; the multipliers resolve in
//   UnitSync's fixed 1 + Mod form). Applied/removed on band crossings by the
//   morale pass (aggregation events, never timers). Fresh ships no modifiers
//   and is never applied -- removing the previous band's GE returns to base.
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectExecutionCalculation.h"
#include "BattleMoraleEffects.generated.h"

class UUnitRegistrySubsystem;

/** The project's ASC (PR-12): the ONLY addition is the public forwarding of
 *  UAbilitySystemComponent::ExecutePeriodicEffect -- the 5.8.4 symbol is
 *  PROTECTED (the Task 3 audit amendment: the natural timer's delegate binds
 *  it from engine-internal friend context, so no external caller exists).
 *  The deterministic pin's forced execution (A2/R-M1) needs exactly that
 *  call; the subclass keeps the engine's own code path (no replication, no
 *  reimplementation). Everything else is inherited verbatim. */
UCLASS()
class MASSBATTLE_API UBattleAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	/** Public forward of the protected base method (the pin's forced period
	 *  execution; game thread only). */
	void ForceExecutePeriodicEffect(FActiveGameplayEffectHandle Handle)
	{
		ExecutePeriodicEffect(Handle);
	}
};

/** The per-unit periodic morale evaluation (A2/ADR-33). One instance active
 *  per unit actor at a time; the handle lives on AUnitActor (the GE
 *  application lifecycle is actor-side, PR-12). */
UCLASS()
class MASSBATTLE_API UBattleMoraleTickEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBattleMoraleTickEffect();
};

/** The one execution calculation (A2): registry-fed stack evaluation writing
 *  the Morale/Fatigue attributes. NO attribute captures -- the inputs come
 *  from the registry block (the disclosed ADR-33 deviation: continuously-
 *  varying sim terms ride the registry, not SetByCaller; there are no
 *  effect-owned terms in 4a, so no SetByCaller stamps exist yet -- the
 *  remove/re-apply refresh path stays for 4b's aggregation-event terms). */
UCLASS()
class MASSBATTLE_API UBattleMoraleExecCalc : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UBattleMoraleExecCalc();

protected:
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};

/** FatigueState catalog base (A8): infinite, no executions; the leaf ctors
 *  build the five additive *Mod modifiers. Not applied for Fresh. */
UCLASS(Abstract)
class MASSBATTLE_API UBattleFatigueStateEffectBase : public UGameplayEffect
{
	GENERATED_BODY()

protected:
	/** Builds the modifier row for State (table minus 1, additive on the
	 *  five *Mod attributes). Called from each leaf ctor. */
	void BuildFromState(int32 StateIndex);
};

UCLASS()
class MASSBATTLE_API UBattleFatigueFreshEffect : public UBattleFatigueStateEffectBase
{
	GENERATED_BODY()
public:
	UBattleFatigueFreshEffect();
};

UCLASS()
class MASSBATTLE_API UBattleFatigueActiveEffect : public UBattleFatigueStateEffectBase
{
	GENERATED_BODY()
public:
	UBattleFatigueActiveEffect();
};

UCLASS()
class MASSBATTLE_API UBattleFatigueQuiteTiredEffect : public UBattleFatigueStateEffectBase
{
	GENERATED_BODY()
public:
	UBattleFatigueQuiteTiredEffect();
};

UCLASS()
class MASSBATTLE_API UBattleFatigueTiredEffect : public UBattleFatigueStateEffectBase
{
	GENERATED_BODY()
public:
	UBattleFatigueTiredEffect();
};

UCLASS()
class MASSBATTLE_API UBattleFatigueVeryTiredEffect : public UBattleFatigueStateEffectBase
{
	GENERATED_BODY()
public:
	UBattleFatigueVeryTiredEffect();
};

UCLASS()
class MASSBATTLE_API UBattleFatigueExhaustedEffect : public UBattleFatigueStateEffectBase
{
	GENERATED_BODY()
public:
	UBattleFatigueExhaustedEffect();
};

/** Catalog lookup: the GE class per EBattleFatigueState index (0..5). */
MASSBATTLE_API TSubclassOf<UGameplayEffect> BattleFatigueEffectClass(int32 StateIndex);
