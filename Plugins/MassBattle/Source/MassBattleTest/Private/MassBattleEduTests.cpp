#if WITH_DEV_AUTOMATION_TESTS
// Battle.Data.* -- T0.2: EDU parser round-trip + canonical golden + edge cases.
// The on-disk fixture (Data/EDU/golden_edu_v1.txt) is the schema source of
// truth; tests load it via FPaths::ProjectDir (per-commit tier runs in the
// editor context, where ProjectDir is the repo root).
// Phase 3 re-sign (2026-10-29, spec ranged-stack section 6): stat_sec joins
// the schema (15 keys) and the fixture gains two ranged rows; the golden and
// every inline edge fixture carry the new field. Disclosed re-sign per the
// T0.2 policy -- any drift is a facts-correction entry (AGENTS section 4).
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"

#include "BattleEduParser.h"

namespace
{
	bool LoadFixture(FString& OutText, FString& OutFailReason)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
		if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path))
		{
			OutFailReason = FString::Printf(TEXT("fixture missing: %s"), *Path);
			return false;
		}
		if (!FFileHelper::LoadFileToString(OutText, *Path))
		{
			OutFailReason = FString::Printf(TEXT("fixture unreadable: %s"), *Path);
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEdu_RoundTripGolden,
	"Battle.Data.EduRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdu_RoundTripGolden::RunTest(const FString& Parameters)
{
	FString Fixture;
	{
		FString FailReason;
		if (!TestTrue(TEXT("fixture loadable"), LoadFixture(Fixture, FailReason)))
		{
			AddError(FailReason);
			return false;
		}
	}

	// fixture -> tables
	TArray<FEduUnit> Units;
	FEduParseError Err;
	const bool bParsed = FBattleEduParser::Parse(Fixture, Units, Err);
	if (!TestTrue(TEXT("fixture parses"), bParsed))
	{
		AddError(FString::Printf(TEXT("parse error line %d: %s"), Err.LineNumber, *Err.Message));
		return false;
	}
	TestEqual(TEXT("schema field count is 15 (phase 3 re-sign: +stat_sec)"), FBattleEduParser::SchemaFieldCount(), 15);
	TestEqual(TEXT("fixture unit count"), Units.Num(), 4);

	// Spot-check the tables against the fixture content.
	const FEduField* Type0 = Units[0].FindField(TEXT("type"));
	const FEduField* Soldier0 = Units[0].FindField(TEXT("soldier"));
	const FEduField* Cost0 = Units[0].FindField(TEXT("stat_cost"));
	TestNotNull(TEXT("unit0 type"), Type0);
	TestNotNull(TEXT("unit0 soldier"), Soldier0);
	TestNotNull(TEXT("unit0 cost"), Cost0);
	if (Type0 && Soldier0 && Cost0)
	{
		// 'type' is EduName (verbatim): case is semantic, compare sensitively.
		TestEqualSensitive(TEXT("unit0 is Peasant Levy"), Type0->GetValue(), FString(TEXT("Peasant Levy")));
		TestEqual(TEXT("unit0 soldier count"), Soldier0->Values[1], FString(TEXT("40")));
		TestEqual(TEXT("unit0 cost upkeep"), Cost0->Values[1], FString(TEXT("100")));
	}
	const FEduField* Class1 = Units[1].FindField(TEXT("class"));
	if (Class1)
	{
		// Token lowercasing is semantically load-bearing: compare case-sensitively.
		TestEqualSensitive(TEXT("unit1 class (token lowercased)"), Class1->GetValue(), FString(TEXT("spear")));
	}
	const FEduField* Pri1 = Units[1].FindField(TEXT("stat_pri"));
	if (Pri1)
	{
		TestEqual(TEXT("unit1 stat_pri value count"), Pri1->Values.Num(), 11);
		TestEqual(TEXT("unit1 stat_pri damage"), Pri1->Values[8], FString(TEXT("25")));
	}
	// Phase 3 ranged rows: stat_pri carries the missile weapon (M2TW
	// convention), stat_sec the melee backup; spot-check the extraction
	// inputs FromEdu consumes (spec ranged-stack section 6, brief F3).
	const FEduField* Pri2 = Units[2].FindField(TEXT("stat_pri"));
	const FEduField* Sec2 = Units[2].FindField(TEXT("stat_sec"));
	TestNotNull(TEXT("unit2 stat_sec present"), Sec2);
	if (Pri2 && Sec2)
	{
		TestEqualSensitive(TEXT("unit2 missile kind (token lowercased)"), Pri2->Values[2], FString(TEXT("arrow")));
		TestEqual(TEXT("unit2 range"), Pri2->Values[3], FString(TEXT("120")));
		TestEqual(TEXT("unit2 ammo"), Pri2->Values[4], FString(TEXT("25")));
		TestEqual(TEXT("unit2 stat_sec melee attack"), Sec2->Values[0], FString(TEXT("4")));
	}
	const FEduField* Type3 = Units[3].FindField(TEXT("type"));
	const FEduField* Pri3 = Units[3].FindField(TEXT("stat_pri"));
	if (Type3 && Pri3)
	{
		TestEqualSensitive(TEXT("unit3 is Mercenary Crossbowmen"), Type3->GetValue(), FString(TEXT("Mercenary Crossbowmen")));
		TestEqualSensitive(TEXT("unit3 missile kind"), Pri3->Values[2], FString(TEXT("bolt")));
		TestEqual(TEXT("unit3 range"), Pri3->Values[3], FString(TEXT("160")));
		TestEqual(TEXT("unit3 ammo"), Pri3->Values[4], FString(TEXT("18")));
	}
	// Melee rows mirror stat_sec with a "no" missile slot.
	const FEduField* Sec0 = Units[0].FindField(TEXT("stat_sec"));
	if (Sec0)
	{
		TestEqualSensitive(TEXT("unit0 stat_sec missile is no"), Sec0->Values[2], FString(TEXT("no")));
	}

	// tables -> canonical text
	// Audit M3: canonical-form equality is byte-sensitive by definition --
	// plain TestEqual compares with Stricmp and is blind to case regressions.
	const FString Canonical1 = FBattleEduParser::SerializeCanonical(Units);

	// canonical text -> tables -> canonical text (fixed point)
	TArray<FEduUnit> Units2;
	FEduParseError Err2;
	if (!TestTrue(TEXT("canonical text reparses"), FBattleEduParser::Parse(Canonical1, Units2, Err2)))
	{
		AddError(FString::Printf(TEXT("reparse error line %d: %s"), Err2.LineNumber, *Err2.Message));
		AddInfo(Canonical1);
		return false;
	}
	const FString Canonical2 = FBattleEduParser::SerializeCanonical(Units2);
	TestEqualSensitive(TEXT("canonical form is a fixed point"), Canonical2, Canonical1);

	// canonical text vs the HAND-WRITTEN canonical golden of the fixture
	const FString GoldenCanonical = FString(
		"type Peasant Levy\n"
		"dictionary Peasant_Levy\n"
		"category infantry\n"
		"class light\n"
		"soldier Peasant, 40, 0, 1\n"
		"attributes sea_faring, hide_forest, very_hardy\n"
		"formation 1.2, 1.2, 2.4, 2.4, 3, square\n"
		"stat_health 1, 1\n"
		"stat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
		"stat_sec 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
		"stat_pri_armour 0, 1, 0, flesh\n"
		"stat_mental 2, low, untrained\n"
		"stat_charge_dist 30\n"
		"stat_cost 1, 100, 90, 40, 60, 90\n"
		"ownership slave, england\n"
		"\n"
		"type Sergeant Spearmen\n"
		"dictionary Sergeant_Spearmen\n"
		"category infantry\n"
		"class spear\n"
		"soldier Armoured_Sergeant, 40, 0, 1\n"
		"attributes sea_faring, hide_forest, hardy, can_withdraw, spear_wall\n"
		"formation 1.2, 1.2, 2.4, 2.4, 3, square\n"
		"stat_health 1, 1\n"
		"stat_pri 4, 4, no, 0, 0, melee, blade_piercing, spear, 25, 0.35, 0.35\n"
		"stat_sec 4, 4, no, 0, 0, melee, blade_piercing, spear, 25, 0.35, 0.35\n"
		"stat_pri_armour 4, 5, 0, metal\n"
		"stat_mental 6, normal, trained\n"
		"stat_charge_dist 30\n"
		"stat_cost 1, 250, 220, 90, 120, 220\n"
		"ownership england, france\n"
		"\n"
		"type Peasant Archers\n"
		"dictionary Peasant_Archers\n"
		"category infantry\n"
		"class missile\n"
		"soldier Peasant_Archer, 40, 0, 1\n"
		"attributes sea_faring, hide_forest, hardy, can_withdraw, can_skirmish\n"
		"formation 1.2, 1.2, 2.4, 2.4, 3, square\n"
		"stat_health 1, 1\n"
		"stat_pri 6, 2, arrow, 120, 25, missile, simple, other, 10, 0.2, 0.2\n"
		"stat_sec 4, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
		"stat_pri_armour 0, 1, 0, flesh\n"
		"stat_mental 4, low, untrained\n"
		"stat_charge_dist 30\n"
		"stat_cost 1, 150, 130, 60, 90, 130\n"
		"ownership slave, england\n"
		"\n"
		"type Mercenary Crossbowmen\n"
		"dictionary Mercenary_Crossbowmen\n"
		"category infantry\n"
		"class missile\n"
		"soldier Mercenary_Crossbow, 40, 0, 1\n"
		"attributes sea_faring, hide_forest, hardy, can_withdraw\n"
		"formation 1.2, 1.2, 2.4, 2.4, 3, square\n"
		"stat_health 1, 1\n"
		"stat_pri 9, 2, bolt, 160, 18, missile, simple, other, 10, 0.25, 0.25\n"
		"stat_sec 5, 3, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
		"stat_pri_armour 3, 4, 0, metal\n"
		"stat_mental 5, normal, trained\n"
		"stat_charge_dist 30\n"
		"stat_cost 1, 300, 260, 100, 130, 260\n"
		"ownership england, france\n");
	TestEqualSensitive(TEXT("canonical text matches hand-written golden"), Canonical1, GoldenCanonical);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEdu_EdgeCases,
	"Battle.Data.EduEdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdu_EdgeCases::RunTest(const FString& Parameters)
{
	// -- unknown fields are rejected LOUDLY (roadmap T0.2) --
	{
		const FString Bad = TEXT("type X\ndictionary X\ncategory infantry\nclass light\n"
			"soldier X, 40, 0, 1\nattributes\nformation 1.2, 1.2, 2.4, 2.4, 3, square\n"
			"stat_health 1, 1\nstat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_sec 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_pri_armour 0, 1, 0, flesh\nstat_mental 2, low, untrained\n"
			"stat_charge_dist 30\nstat_cost 1, 100, 90, 40, 60, 90\nownership england\n"
			"stat_magic 5\n"); // <- unknown
		TArray<FEduUnit> Units;
		FEduParseError Err;
		TestFalse(TEXT("unknown field fails parse"), FBattleEduParser::Parse(Bad, Units, Err));
		TestTrue(TEXT("error flagged"), Err.bFailed);
		TestTrue(TEXT("error names the unknown field"), Err.Message.Contains(TEXT("stat_magic")));
		AddInfo(FString::Printf(TEXT("loud rejection: line %d: %s"), Err.LineNumber, *Err.Message));
	}

	// -- duplicates, wrong arity, malformed numbers --
	{
		const FString Dup = TEXT("type X\ntype Y\ndictionary X\ncategory infantry\nclass light\n"
			"soldier X, 40, 0, 1\nattributes\nformation 1.2, 1.2, 2.4, 2.4, 3, square\n"
			"stat_health 1, 1\nstat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_sec 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_pri_armour 0, 1, 0, flesh\nstat_mental 2, low, untrained\n"
			"stat_charge_dist 30\nstat_cost 1, 100, 90, 40, 60, 90\nownership england\n");
		TArray<FEduUnit> Units;
		FEduParseError Err;
		TestFalse(TEXT("duplicate field fails"), FBattleEduParser::Parse(Dup, Units, Err));

		const FString BadArity = TEXT("type X\ndictionary X\ncategory infantry\nclass light\n"
			"soldier X, 40\nattributes\nformation 1.2, 1.2, 2.4, 2.4, 3, square\n" // soldier needs 4
			"stat_health 1, 1\nstat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_sec 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_pri_armour 0, 1, 0, flesh\nstat_mental 2, low, untrained\n"
			"stat_charge_dist 30\nstat_cost 1, 100, 90, 40, 60, 90\nownership england\n");
		Err = FEduParseError();
		TestFalse(TEXT("wrong arity fails"), FBattleEduParser::Parse(BadArity, Units, Err));

		const FString BadSecArity = TEXT("type X\ndictionary X\ncategory infantry\nclass light\n"
			"soldier X, 40, 0, 1\nattributes\nformation 1.2, 1.2, 2.4, 2.4, 3, square\n"
			"stat_health 1, 1\nstat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_sec 4, 2\n" // <- stat_sec needs 11
			"stat_pri_armour 0, 1, 0, flesh\nstat_mental 2, low, untrained\n"
			"stat_charge_dist 30\nstat_cost 1, 100, 90, 40, 60, 90\nownership england\n");
		Err = FEduParseError();
		TestFalse(TEXT("wrong stat_sec arity fails"), FBattleEduParser::Parse(BadSecArity, Units, Err));

		const FString MissingSec = TEXT("type X\ndictionary X\ncategory infantry\nclass light\n"
			"soldier X, 40, 0, 1\nattributes\nformation 1.2, 1.2, 2.4, 2.4, 3, square\n"
			"stat_health 1, 1\nstat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_pri_armour 0, 1, 0, flesh\nstat_mental 2, low, untrained\n"
			"stat_charge_dist 30\nstat_cost 1, 100, 90, 40, 60, 90\nownership england\n");
		Err = FEduParseError();
		TestFalse(TEXT("missing stat_sec fails (schema completeness)"), FBattleEduParser::Parse(MissingSec, Units, Err));

		const FString BadNum = TEXT("type X\ndictionary X\ncategory infantry\nclass light\n"
			"soldier X, 40, 0, 1\nattributes\nformation 1.2, 1.2, 2.4, 2.4, 3, square\n"
			"stat_health abc, 1\n" // <- not an int
			"stat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_sec 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_pri_armour 0, 1, 0, flesh\nstat_mental 2, low, untrained\n"
			"stat_charge_dist 30\nstat_cost 1, 100, 90, 40, 60, 90\nownership england\n");
		Err = FEduParseError();
		TestFalse(TEXT("malformed number fails"), FBattleEduParser::Parse(BadNum, Units, Err));
	}

	// -- normalization: extra whitespace, comments, numeric forms --
	{
		const FString Messy = FString(
			"// header comment kept out of canonical form\n"
			"type X\n") +
			TEXT("  dictionary   X  \n"
			"category Infantry\n"
			"class light\n"
			"soldier X, 040, 0, 1\n"          // leading-zero int
			"attributes\n"                     // empty list is legal (min 0)
			"formation 1.20, 1.2, 2.40, 2.4, 3.00, square\n" // float forms
			"stat_health 1, 1\n"
			"stat_pri 07, 2, NO, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_sec 4, 2, ARROW, 120, 025, missile, simple, other, 10, 0.2, 0.2\n"
			"stat_pri_armour 0, 1, 0, flesh\n"
			"stat_mental 2, low, untrained\n"
			"stat_charge_dist 30\n"
			"stat_cost 1, 100, 90, 40, 60, 90\n"
			"ownership england\n"
			"\n"
			"// trailing comment block\n");
		FString Canonical;
		FEduParseError Err;
		if (!TestTrue(TEXT("messy input canonicalizes"), FBattleEduParser::Canonicalize(Messy, Canonical, Err)))
		{
			AddError(FString::Printf(TEXT("line %d: %s"), Err.LineNumber, *Err.Message));
			return false;
		}

		const FString Expected = FString(
			"type X\n") +
			TEXT("dictionary X\n" // EduName kind: VERBATIM, not lowercased (canonicalization rule 4).
			"category infantry\n"
			"class light\n"
			"soldier X, 40, 0, 1\n"       // leading zeros stripped; name kept verbatim
			"attributes\n"
			"formation 1.2, 1.2, 2.4, 2.4, 3, square\n" // float forms normalized
			"stat_health 1, 1\n"
			"stat_pri 7, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_sec 4, 2, arrow, 120, 25, missile, simple, other, 10, 0.2, 0.2\n"
			"stat_pri_armour 0, 1, 0, flesh\n"
			"stat_mental 2, low, untrained\n"
			"stat_charge_dist 30\n"
			"stat_cost 1, 100, 90, 40, 60, 90\n"
			"ownership england\n");
		// Audit M3: byte-sensitive compare. The old expectation lowercased the
		// EduName values ('soldier x') contradicting the parser contract; the
		// old case-INsensitive TestEqual hid the contradiction. This now fails
		// loudly if EduName handling or the canonical form changes case.
		TestEqualSensitive(TEXT("messy canonical matches expected normalization"), Canonical, Expected);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
