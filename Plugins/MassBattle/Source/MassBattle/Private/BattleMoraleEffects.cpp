// BattleMoraleEffects.cpp -- the MoraleTick pipeline's GAS half (PR-12; spec
// morale-and-gas sections 4.8-4.10). Engine-symbol shapes verified in Task 3
// (engine-api Part A section 16): exec outputs apply through
// ApplyModToAttribute (Override sets the BASE value -- the per-tick absolute
// write); FGameplayEffectConstants::NO_PERIOD kills the natural timer at the
// :4497 application gate.
#include "BattleMoraleEffects.h"
#include "BattleMorale.h"
#include "BattleRegistry.h"
#include "UnitActor.h"
#include "BattleFragments.h"

#include "GameplayEffectTypes.h"

namespace
{
	/** The Fatigue attribute carries the exertion integral clamped to the
	 *  Exhausted band edge (a 0..100 display-scale meta value; the OLD
	 *  placeholder coupling read Fatigue * 0.01, implying this scale -- the
	 *  value has no consumer this phase, the hash mirror carries it). */
	constexpr float FatigueAttrClamp = 100.f;
}

UBattleMoraleTickEffect::UBattleMoraleTickEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	// Placeholder Period on the def; the APPLICATION overrides the spec's
	// Period per mode (AUnitActor::ApplyMoraleTickEffect): NO_PERIOD under
	// the deterministic pin (no natural timer), TickPeriod in shipping. The
	// def value only matters for a bare ApplyGameplayEffectToSelf call, which
	// the pipeline never makes.
	Period = FScalableFloat(0.35f);

	FGameplayEffectExecutionDefinition ExecDef;
	ExecDef.CalculationClass = UBattleMoraleExecCalc::StaticClass();
	Executions.Add(ExecDef);
}

UBattleMoraleExecCalc::UBattleMoraleExecCalc()
{
	// No captures: every input rides the registry block (see header).
}

void UBattleMoraleExecCalc::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	// Master flag first: a flags-off flip mid-session leaves active MoraleTick
	// GEs behind (they are removed only by the pass/actor teardown); the
	// no-op keeps them from writing values (defense in depth -- the pass
	// never applies under Enable=0 in the first place).
	if (!FMoraleCVars::Enable())
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
	AUnitActor* OwnerActor = TargetASC ? Cast<AUnitActor>(TargetASC->GetOwnerActor()) : nullptr;
	UUnitRegistrySubsystem* Registry = OwnerActor && OwnerActor->GetWorld()
		? OwnerActor->GetWorld()->GetSubsystem<UUnitRegistrySubsystem>()
		: nullptr;
	const FUnitHandle Unit = OwnerActor ? OwnerActor->GetUnitHandle() : FUnitHandle();
	if (!Registry || !Unit.IsValid())
	{
		return; // no sim state (unspawned actor / post-reset world) -> no write
	}

	const FMoraleTickInputs* Inputs = Registry->FindMoraleInputs(Unit);
	const FUnitRuntimeState* State = Registry->FindUnitState(Unit);
	if (!Inputs || !State)
	{
		return;
	}

	// A14: ONE stack evaluation per tick, HERE (the pass never evaluates it).
	const float Morale = EvaluateMorale(*Inputs);
	const float Fatigue = FMath::Clamp(State->ExertionIntegral, 0.f, FatigueAttrClamp);

	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
		UUnitAttributeSet::GetMoraleAttribute(), EGameplayModOp::Override, Morale));
	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
		UUnitAttributeSet::GetFatigueAttribute(), EGameplayModOp::Override, Fatigue));

	// The staleness stamp (T4.8/MC1): the exec calc is the only party that
	// knows the value is fresh NOW. World GAME seconds = sim time (determinism
	// safe: excluded from the hash per A10; the ban is wall-clock). A single
	// float write on the runtime state; the read side is the staleness probe.
	if (FUnitRuntimeState* MutableState = const_cast<FUnitRuntimeState*>(State))
	{
		MutableState->LastMoraleTickSimTime = OwnerActor->GetWorld()->GetTimeSeconds();
	}
}

// -- FatigueState catalog (A8) --

void UBattleFatigueStateEffectBase::BuildFromState(int32 StateIndex)
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	const FFatigueMultipliers Table = GetFatigueMultipliers(static_cast<EBattleFatigueState>(StateIndex));

	// The table row minus 1, additive on the *Mod attributes (ADR-09); the
	// multipliers resolve in UnitSync's fixed 1 + Mod form (ADR-36). Fresh is
	// all-1.0 -> zero modifiers -> never applied (removal returns to base).
	// The table v1 degrades ATTACK/DEFENSE/SPEED/RELOAD; there is no
	// block/reload attribute in the closed set, so BlockChanceMod stays
	// untouched this phase and the reload multiplier rides RangedAttackMod
	// (entity-visible in the ranged pass until the T6.1 audit rules on the
	// reload surface -- disclosed in the phase report).
	if (Table.AttackMult != 1.f)
	{
		FGameplayModifierInfo Mod;
		Mod.Attribute = UUnitAttributeSet::GetMeleeAttackModAttribute();
		Mod.ModifierOp = EGameplayModOp::Additive;
		Mod.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Table.AttackMult - 1.f));
		Modifiers.Add(Mod);
	}
	if (Table.DefenseMult != 1.f)
	{
		FGameplayModifierInfo Mod;
		Mod.Attribute = UUnitAttributeSet::GetMeleeDefenseModAttribute();
		Mod.ModifierOp = EGameplayModOp::Additive;
		Mod.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Table.DefenseMult - 1.f));
		Modifiers.Add(Mod);
	}
	if (Table.SpeedMult != 1.f)
	{
		FGameplayModifierInfo Mod;
		Mod.Attribute = UUnitAttributeSet::GetSpeedModAttribute();
		Mod.ModifierOp = EGameplayModOp::Additive;
		Mod.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Table.SpeedMult - 1.f));
		Modifiers.Add(Mod);
	}
	if (Table.ReloadMult != 1.f)
	{
		FGameplayModifierInfo Mod;
		Mod.Attribute = UUnitAttributeSet::GetRangedAttackModAttribute();
		Mod.ModifierOp = EGameplayModOp::Additive;
		Mod.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Table.ReloadMult - 1.f));
		Modifiers.Add(Mod);
	}
}

#define BATTLE_FATIGUE_EFFECT_CTOR(Class, StateEnum) \
	Class::Class() { BuildFromState(static_cast<int32>(StateEnum)); }

BATTLE_FATIGUE_EFFECT_CTOR(UBattleFatigueFreshEffect, EBattleFatigueState::Fresh)
BATTLE_FATIGUE_EFFECT_CTOR(UBattleFatigueActiveEffect, EBattleFatigueState::Active)
BATTLE_FATIGUE_EFFECT_CTOR(UBattleFatigueQuiteTiredEffect, EBattleFatigueState::QuiteTired)
BATTLE_FATIGUE_EFFECT_CTOR(UBattleFatigueTiredEffect, EBattleFatigueState::Tired)
BATTLE_FATIGUE_EFFECT_CTOR(UBattleFatigueVeryTiredEffect, EBattleFatigueState::VeryTired)
BATTLE_FATIGUE_EFFECT_CTOR(UBattleFatigueExhaustedEffect, EBattleFatigueState::Exhausted)

#undef BATTLE_FATIGUE_EFFECT_CTOR

TSubclassOf<UGameplayEffect> BattleFatigueEffectClass(int32 StateIndex)
{
	switch (static_cast<EBattleFatigueState>(FMath::Clamp(StateIndex, 0, 5)))
	{
	case EBattleFatigueState::Active:    return UBattleFatigueActiveEffect::StaticClass();
	case EBattleFatigueState::QuiteTired: return UBattleFatigueQuiteTiredEffect::StaticClass();
	case EBattleFatigueState::Tired:     return UBattleFatigueTiredEffect::StaticClass();
	case EBattleFatigueState::VeryTired: return UBattleFatigueVeryTiredEffect::StaticClass();
	case EBattleFatigueState::Exhausted: return UBattleFatigueExhaustedEffect::StaticClass();
	case EBattleFatigueState::Fresh:
	default:                             return nullptr; // Fresh = no active GE
	}
}
