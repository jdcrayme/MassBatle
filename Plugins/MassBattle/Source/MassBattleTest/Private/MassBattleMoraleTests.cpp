#if WITH_DEV_AUTOMATION_TESTS
// MassBattleMoraleTests.cpp -- Phase 4a morale gates (spec morale-and-gas §7).
// T4.1 (Battle.Unit.Morale.Stack): the contribution stack goldens -- each
//   term isolated at its known weight, the decay curve, the threshold order.
// T4.2 (Battle.Unit.Morale.Fatigue): the exertion integral -> state bands ->
//   multiplier table goldens.
// RoutRally (Battle.Unit.Morale.RoutRally): the FSM transition matrix + the
//   rally roll math (the T4.1 "thresholds fire in order" half, split for the
//   C-3 one-home discipline: disjoint suites, no coverage overlap).
//
// Golden posture: closed-form property checks with tight tolerances (1e-4)
// over the seam's own constants -- the constants are the A16 placeholders,
// and these tests lock THEM (a weight change must be a disclosed re-sign).
#include "Misc/AutomationTest.h"

#include "BattleMorale.h"
#include "BattleTypes.h"

// ---------------------------------------------------------------------------
// T4.1: Battle.Unit.Morale.Stack -- each contribution isolated at a known
// weight; the spike decay per curve; the threshold order.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMorale_Stack,
	"Battle.Unit.Morale.Stack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMorale_Stack::RunTest(const FString& Parameters)
{
	// -- the zero stack: only the base survives.
	{
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 6.f;
		TestEqual(TEXT("zero stack = base"), EvaluateMorale(Inputs), 6.f);
	}

	// -- each term isolated at its known weight (the MC2 golden: a weight
	//    change is a disclosed re-sign, never silent).
	{
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 10.f;
		Inputs.CasualtyFraction = 0.5f;
		TestEqual(TEXT("casualty term weight"), EvaluateMorale(Inputs),
			10.f + FMoraleWeights::CasualtyFraction * 0.5f);
	}
	{
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 10.f;
		Inputs.FreshCasualtySpike = 4.f;
		TestEqual(TEXT("spike term weight"), EvaluateMorale(Inputs),
			10.f + FMoraleWeights::FreshCasualtySpike * 4.f);
	}
	{
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 10.f;
		Inputs.LocalOdds = -1.f; // badly losing locally
		TestEqual(TEXT("local odds weight"), EvaluateMorale(Inputs),
			10.f + FMoraleWeights::LocalOdds * -1.f);
	}
	{
		// The arc term counts SET BITS (engaged sectors), not the mask value.
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 10.f;
		Inputs.ArcExposureMask = 0b0000'0101; // two sectors
		TestEqual(TEXT("arc exposure per set bit"), EvaluateMorale(Inputs),
			10.f + FMoraleWeights::ArcExposurePerSector * 2.f);
	}
	{
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 10.f;
		Inputs.RoutingFriendsNearby = 2;
		TestEqual(TEXT("routing friends weight"), EvaluateMorale(Inputs),
			10.f + FMoraleWeights::RoutingFriend * 2.f);
	}
	{
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 10.f;
		Inputs.GeneralAuraBonus = 1.5f;
		TestEqual(TEXT("general aura weight"), EvaluateMorale(Inputs),
			10.f + FMoraleWeights::GeneralAura * 1.5f);
	}
	{
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 10.f;
		Inputs.GeneralDeathShock = 6.f;
		TestEqual(TEXT("general death shock weight"), EvaluateMorale(Inputs),
			10.f + FMoraleWeights::GeneralDeathShock * 6.f);
	}
	{
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 10.f;
		Inputs.HeightAdvantage = 1.f;
		TestEqual(TEXT("height advantage weight"), EvaluateMorale(Inputs),
			10.f + FMoraleWeights::HeightAdvantage * 1.f);
	}
	{
		FMoraleTickInputs Inputs;
		Inputs.BaseMorale = 10.f;
		Inputs.WinningLocally = 0.5f;
		TestEqual(TEXT("winning locally weight"), EvaluateMorale(Inputs),
			10.f + FMoraleWeights::WinningLocally * 0.5f);
	}

	// -- penalties are negative, bonuses positive (the sign contract).
	TestTrue(TEXT("casualty weight is a penalty"), FMoraleWeights::CasualtyFraction < 0.f);
	TestTrue(TEXT("spike weight is a penalty"), FMoraleWeights::FreshCasualtySpike < 0.f);
	TestTrue(TEXT("odds weight is a penalty"), FMoraleWeights::LocalOdds < 0.f);
	TestTrue(TEXT("arc weight is a penalty"), FMoraleWeights::ArcExposurePerSector < 0.f);
	TestTrue(TEXT("cascade weight is a penalty"), FMoraleWeights::RoutingFriend < 0.f);
	TestTrue(TEXT("shock weight is a penalty"), FMoraleWeights::GeneralDeathShock < 0.f);
	TestTrue(TEXT("aura weight is a bonus"), FMoraleWeights::GeneralAura > 0.f);
	TestTrue(TEXT("height weight is a bonus"), FMoraleWeights::HeightAdvantage > 0.f);
	TestTrue(TEXT("winning weight is a bonus"), FMoraleWeights::WinningLocally > 0.f);

	// -- the decay curve (spec §4.2): the half-life property is exact.
	{
		TestEqual(TEXT("half-life is exact"), EvaluateDecay(4.f, 4.f, 4.f), 2.f, 1e-4f);
		TestEqual(TEXT("two half-lives"), EvaluateDecay(8.f, 8.f, 4.f), 2.f, 1e-4f);
		TestEqual(TEXT("zero dt is identity"), EvaluateDecay(5.f, 0.f, 4.f), 5.f, 1e-6f);
		TestEqual(TEXT("non-positive half-life is identity"), EvaluateDecay(5.f, 10.f, 0.f), 5.f, 1e-6f);
		// Monotone non-increasing in Dt.
		const float Fast = EvaluateDecay(6.f, 1.f, 4.f);
		const float Slow = EvaluateDecay(6.f, 3.f, 4.f);
		TestTrue(TEXT("decay is monotone in dt"), Slow <= Fast);
	}

	// -- the threshold order (T4.1's "thresholds fire in order"): a high
	//    morale sweep descends Steady -> Wavering -> Routing, and the
	//    recovery needs the hysteresis margin.
	{
		const float Wavering = 4.f;
		const float Break = 0.f;
		const float Hysteresis = 1.f;
		const EBattleDiscipline Normal = EBattleDiscipline::Normal;
		TestEqual(TEXT("high morale is steady"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Steady, 8.f, Wavering, Break, Hysteresis, 0, 2, Normal)),
			static_cast<int32>(EBattleRoutState::Steady));
		TestEqual(TEXT("below wavering wavers"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Steady, 3.5f, Wavering, Break, Hysteresis, 0, 2, Normal)),
			static_cast<int32>(EBattleRoutState::Wavering));
		TestEqual(TEXT("below break routes"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Wavering, -1.f, Wavering, Break, Hysteresis, 0, 2, Normal)),
			static_cast<int32>(EBattleRoutState::Routing));
		// The recovery hysteresis: above wavering but below wavering+hysteresis
		// a wavering unit STAYS wavering.
		TestEqual(TEXT("wavering holds below the hysteresis margin"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Wavering, 4.5f, Wavering, Break, Hysteresis, 0, 2, Normal)),
			static_cast<int32>(EBattleRoutState::Wavering));
		TestEqual(TEXT("wavering recovers past the margin"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Wavering, 5.5f, Wavering, Break, Hysteresis, 0, 2, Normal)),
			static_cast<int32>(EBattleRoutState::Steady));
		// A unit already below the break threshold breaks from Steady too
		// (the FSM never skips the check on entry state).
		TestEqual(TEXT("steady below break routes directly"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Steady, -1.f, Wavering, Break, Hysteresis, 0, 2, Normal)),
			static_cast<int32>(EBattleRoutState::Routing));
	}

	// -- discipline shifts the BREAK threshold, never the weights (MC2).
	//    Additive offsets (FMoraleDiscipline::BreakOffset): Low +2, Normal 0,
	//    Disciplined -2 -- a disciplined unit holds two points longer.
	{
		const float Wavering = 4.f;
		const float Break = 0.f;
		// Morale -1 with Normal discipline: below the break threshold -> routs.
		TestEqual(TEXT("normal breaks below the raw threshold"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Wavering, -1.f, Wavering, Break, 1.f, 0, 2, EBattleDiscipline::Normal)),
			static_cast<int32>(EBattleRoutState::Routing));
		// The same morale with Disciplined: effective break -2, -1 holds -> only
		// wavering (it keeps fighting where the normal unit broke).
		TestEqual(TEXT("disciplined holds below the raw threshold"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Wavering, -1.f, Wavering, Break, 1.f, 0, 2, EBattleDiscipline::Disciplined)),
			static_cast<int32>(EBattleRoutState::Wavering));
		// Low discipline: effective break +2, so morale 1 already routs.
		TestEqual(TEXT("low discipline breaks two points early"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Wavering, 1.f, Wavering, Break, 1.f, 0, 2, EBattleDiscipline::Low)),
			static_cast<int32>(EBattleRoutState::Routing));
		// Impetuous fights like Normal at the break (its edge is the auto-charge).
		TestEqual(TEXT("impetuous breaks like normal"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Wavering, -1.f, Wavering, Break, 1.f, 0, 2, EBattleDiscipline::Impetuous)),
			static_cast<int32>(EBattleRoutState::Routing));
	}

	return true;
}

// ---------------------------------------------------------------------------
// T4.2: Battle.Unit.Morale.Fatigue -- the exertion integral, the state bands,
// and the multiplier table golden.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMorale_Fatigue,
	"Battle.Unit.Morale.Fatigue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMorale_Fatigue::RunTest(const FString& Parameters)
{
	// -- the exertion integral (spec §4.3): rates sum when both activities
	//    hold; walking/idle adds nothing; the rate scale multiplies.
	{
		float Exertion = 0.f;
		AccumulateExertion(Exertion, 10.f, true, false);
		TestEqual(TEXT("running 10s at 0.6/s"), Exertion, 6.f, 1e-4f);
	}
	{
		float Exertion = 0.f;
		AccumulateExertion(Exertion, 10.f, false, true);
		TestEqual(TEXT("fighting 10s at 0.8/s"), Exertion, 8.f, 1e-4f);
	}
	{
		float Exertion = 0.f;
		AccumulateExertion(Exertion, 10.f, true, true);
		TestEqual(TEXT("both activities sum their rates"), Exertion, 14.f, 1e-4f);
	}
	{
		float Exertion = 5.f;
		AccumulateExertion(Exertion, 30.f, false, false);
		TestEqual(TEXT("idle adds nothing"), Exertion, 5.f, 1e-6f);
	}
	{
		float Exertion = 0.f;
		AccumulateExertion(Exertion, 10.f, true, false, 0.5f);
		TestEqual(TEXT("hardy halves the rate"), Exertion, 3.f, 1e-4f);
	}
	{
		// Monotone: the integral never decreases (no rest model this phase).
		float Exertion = 10.f;
		AccumulateExertion(Exertion, -5.f, true, true);
		TestTrue(TEXT("negative dt never drains"), Exertion >= 10.f);
	}

	// -- the state bands (the six-band ladder; boundaries are inclusive-low).
	{
		TestEqual(TEXT("0 is fresh"), static_cast<int32>(EvaluateFatigueState(0.f)), static_cast<int32>(EBattleFatigueState::Fresh));
		TestEqual(TEXT("band edge - epsilon is fresh"), static_cast<int32>(EvaluateFatigueState(9.99f)), static_cast<int32>(EBattleFatigueState::Fresh));
		TestEqual(TEXT("band edge is active"), static_cast<int32>(EvaluateFatigueState(10.f)), static_cast<int32>(EBattleFatigueState::Active));
		TestEqual(TEXT("25 is quite tired"), static_cast<int32>(EvaluateFatigueState(25.f)), static_cast<int32>(EBattleFatigueState::QuiteTired));
		TestEqual(TEXT("45 is tired"), static_cast<int32>(EvaluateFatigueState(45.f)), static_cast<int32>(EBattleFatigueState::Tired));
		TestEqual(TEXT("70 is very tired"), static_cast<int32>(EvaluateFatigueState(70.f)), static_cast<int32>(EBattleFatigueState::VeryTired));
		TestEqual(TEXT("100 is exhausted"), static_cast<int32>(EvaluateFatigueState(100.f)), static_cast<int32>(EBattleFatigueState::Exhausted));
		TestEqual(TEXT("1e6 is exhausted"), static_cast<int32>(EvaluateFatigueState(1e6f)), static_cast<int32>(EBattleFatigueState::Exhausted));
	}

	// -- the multiplier table (T4.2 golden): the community model's shape --
	//    the first step off Fresh costs ONLY reload; melee degrades from
	//    QuiteTired; everything degrades monotonically to Exhausted.
	{
		const FFatigueMultipliers Fresh = GetFatigueMultipliers(EBattleFatigueState::Fresh);
		TestEqual(TEXT("fresh attack"), Fresh.AttackMult, 1.f, 1e-5f);
		TestEqual(TEXT("fresh defense"), Fresh.DefenseMult, 1.f, 1e-5f);
		TestEqual(TEXT("fresh speed"), Fresh.SpeedMult, 1.f, 1e-5f);
		TestEqual(TEXT("fresh reload"), Fresh.ReloadMult, 1.f, 1e-5f);

		const FFatigueMultipliers Active = GetFatigueMultipliers(EBattleFatigueState::Active);
		TestEqual(TEXT("active attack unchanged"), Active.AttackMult, 1.f, 1e-5f);
		TestEqual(TEXT("active defense unchanged"), Active.DefenseMult, 1.f, 1e-5f);
		TestEqual(TEXT("active speed unchanged"), Active.SpeedMult, 1.f, 1e-5f);
		TestEqual(TEXT("active reload 0.95"), Active.ReloadMult, 0.95f, 1e-5f);

		const FFatigueMultipliers Exhausted = GetFatigueMultipliers(EBattleFatigueState::Exhausted);
		TestEqual(TEXT("exhausted attack 0.55"), Exhausted.AttackMult, 0.55f, 1e-5f);
		TestEqual(TEXT("exhausted defense 0.65"), Exhausted.DefenseMult, 0.65f, 1e-5f);
		TestEqual(TEXT("exhausted speed 0.75"), Exhausted.SpeedMult, 0.75f, 1e-5f);
		TestEqual(TEXT("exhausted reload 0.55"), Exhausted.ReloadMult, 0.55f, 1e-5f);

		// Monotone non-increasing across the ladder, every column.
		const EBattleFatigueState Ladder[6] = {
			EBattleFatigueState::Fresh, EBattleFatigueState::Active, EBattleFatigueState::QuiteTired,
			EBattleFatigueState::Tired, EBattleFatigueState::VeryTired, EBattleFatigueState::Exhausted };
		for (int32 Column = 0; Column < 4; ++Column)
		{
			float Previous = 2.f;
			for (const EBattleFatigueState State : Ladder)
			{
				const FFatigueMultipliers Row = GetFatigueMultipliers(State);
				const float Value = Column == 0 ? Row.AttackMult : Column == 1 ? Row.DefenseMult
					: Column == 2 ? Row.SpeedMult : Row.ReloadMult;
				TestTrue(FString::Printf(TEXT("column %d monotone at state %d"), Column, static_cast<int32>(State)),
					Value <= Previous + 1e-6f);
				Previous = Value;
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// RoutRally: Battle.Unit.Morale.RoutRally -- the FSM's rout persistence, the
// Shattered lockout, and the rally roll math (T4.3's seam half).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMorale_RoutRally,
	"Battle.Unit.Morale.RoutRally",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMorale_RoutRally::RunTest(const FString& Parameters)
{
	// -- Routing persists: the FSM never self-recovers (rally is the only
	//    exit, and it is the REGISTRY's apply -- spec §4.4).
	{
		TestEqual(TEXT("routing stays routing"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Routing, 20.f, 4.f, 0.f, 1.f, 1, 2, EBattleDiscipline::Normal)),
			static_cast<int32>(EBattleRoutState::Routing));
	}

	// -- the Shattered lockout: at the rally-exhaustion limit Routing
	//    latches Shattered (terminal).
	{
		TestEqual(TEXT("routing at the cap shatters"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Routing, 20.f, 4.f, 0.f, 1.f, 2, 2, EBattleDiscipline::Normal)),
			static_cast<int32>(EBattleRoutState::Shattered));
		TestEqual(TEXT("shattered is terminal"),
			static_cast<int32>(EvaluateRoutTransition(EBattleRoutState::Shattered, 100.f, 4.f, 0.f, 1.f, 0, 2, EBattleDiscipline::Normal)),
			static_cast<int32>(EBattleRoutState::Shattered));
	}

	// -- the rally chance (spec §4.5, MC6): both preconditions, the decay,
	//    and the discipline scaling.
	{
		// No aura -> zero chance regardless of morale or roll.
		TestEqual(TEXT("no aura means no rally chance"),
			EvaluateRallyChance(10.f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Normal, 0, false), 0.f, 1e-6f);
		// Below threshold -> zero even with the aura.
		TestEqual(TEXT("below threshold means no rally chance"),
			EvaluateRallyChance(5.9f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Normal, 0, true), 0.f, 1e-6f);
		// The base chance at the clean point.
		TestEqual(TEXT("base chance at zero breaks"),
			EvaluateRallyChance(10.f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Normal, 0, true), 0.4f, 1e-4f);
		// The per-break decay: 0.4 * 0.5^2 = 0.1.
		TestEqual(TEXT("chance decays per break"),
			EvaluateRallyChance(10.f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Normal, 2, true), 0.1f, 1e-4f);
		// The discipline factors (impetuous worst, disciplined best).
		const float NormalChance = EvaluateRallyChance(10.f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Normal, 1, true);
		const float ImpetuousChance = EvaluateRallyChance(10.f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Impetuous, 1, true);
		const float DisciplinedChance = EvaluateRallyChance(10.f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Disciplined, 1, true);
		TestTrue(TEXT("impetuous rallies worse than normal"), ImpetuousChance < NormalChance);
		TestTrue(TEXT("disciplined rallies better than normal"), DisciplinedChance > NormalChance);
	}

	// -- the rally decision: the roll vs the chance (strictly below).
	{
		TestTrue(TEXT("good roll rallies under the aura"),
			EvaluateRally(10.f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Normal, 0, true, 0.3f));
		TestFalse(TEXT("bad roll fails under the aura"),
			EvaluateRally(10.f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Normal, 0, true, 0.5f));
		TestFalse(TEXT("perfect roll fails without the aura"),
			EvaluateRally(10.f, 6.f, 0.4f, 0.5f, EBattleDiscipline::Normal, 0, false, 0.f));
		TestFalse(TEXT("zero chance never rallies"),
			EvaluateRally(10.f, 6.f, 0.f, 0.5f, EBattleDiscipline::Normal, 0, true, 0.f));
	}

	// -- the full FSM cycle a scenario drives end to end (T4.3's seam
	//    rehearsal): Steady -> Wavering -> Routing -> (rally) -> Wavering
	//    -> Steady, with the Shattered variant at the cap.
	{
		const float Wavering = 4.f;
		const float Break = 0.f;
		const float Hysteresis = 1.f;
		EBattleRoutState State = EBattleRoutState::Steady;
		State = EvaluateRoutTransition(State, 3.f, Wavering, Break, Hysteresis, 0, 2, EBattleDiscipline::Normal);
		TestEqual(TEXT("cycle: waver"), static_cast<int32>(State), static_cast<int32>(EBattleRoutState::Wavering));
		State = EvaluateRoutTransition(State, -2.f, Wavering, Break, Hysteresis, 1, 2, EBattleDiscipline::Normal);
		TestEqual(TEXT("cycle: break"), static_cast<int32>(State), static_cast<int32>(EBattleRoutState::Routing));
		// The registry applies the rally (Wavering re-entry) out-of-seam;
		// from Wavering the recovery path needs the margin.
		State = EBattleRoutState::Wavering;
		State = EvaluateRoutTransition(State, 6.f, Wavering, Break, Hysteresis, 1, 2, EBattleDiscipline::Normal);
		TestEqual(TEXT("cycle: recover"), static_cast<int32>(State), static_cast<int32>(EBattleRoutState::Steady));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
