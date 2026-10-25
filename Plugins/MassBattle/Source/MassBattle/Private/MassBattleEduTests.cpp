// Battle.Data.* -- T0.2: EDU parser round-trip + canonical golden + edge cases.
// The on-disk fixture (Data/EDU/golden_edu_v1.txt) is the schema source of
// truth; tests load it via FPaths::ProjectDir (per-commit tier runs in the
// editor context, where ProjectDir is the repo root).
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
	TestEqual(TEXT("schema field count is 14 (reality-corrected)"), FBattleEduParser::SchemaFieldCount(), 14);
	TestEqual(TEXT("fixture unit count"), Units.Num(), 2);

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
		"stat_pri_armour 4, 5, 0, metal\n"
		"stat_mental 6, normal, trained\n"
		"stat_charge_dist 30\n"
		"stat_cost 1, 250, 220, 90, 120, 220\n"
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
			"stat_pri_armour 0, 1, 0, flesh\nstat_mental 2, low, untrained\n"
			"stat_charge_dist 30\nstat_cost 1, 100, 90, 40, 60, 90\nownership england\n");
		TArray<FEduUnit> Units;
		FEduParseError Err;
		TestFalse(TEXT("duplicate field fails"), FBattleEduParser::Parse(Dup, Units, Err));

		const FString BadArity = TEXT("type X\ndictionary X\ncategory infantry\nclass light\n"
			"soldier X, 40\nattributes\nformation 1.2, 1.2, 2.4, 2.4, 3, square\n" // soldier needs 4
			"stat_health 1, 1\nstat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
			"stat_pri_armour 0, 1, 0, flesh\nstat_mental 2, low, untrained\n"
			"stat_charge_dist 30\nstat_cost 1, 100, 90, 40, 60, 90\nownership england\n");
		Err = FEduParseError();
		TestFalse(TEXT("wrong arity fails"), FBattleEduParser::Parse(BadArity, Units, Err));

		const FString BadNum = TEXT("type X\ndictionary X\ncategory infantry\nclass light\n"
			"soldier X, 40, 0, 1\nattributes\nformation 1.2, 1.2, 2.4, 2.4, 3, square\n"
			"stat_health abc, 1\n" // <- not an int
			"stat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n"
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
