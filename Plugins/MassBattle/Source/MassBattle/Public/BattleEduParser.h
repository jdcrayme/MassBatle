// BattleEduParser.h -- pure EDU-format parser/serializer (roadmap R11/B.0
// pure-math seam: free functions + plain structs, no UObject, fully unit-testable).
//
// Schema source of truth: Data/EDU/golden_edu_v1.txt -- 14 field keys per unit
// (BRIEF §1.8 as corrected by reality during this run):
//   type, dictionary, category, class, soldier, attributes, formation,
//   stat_health, stat_pri, stat_pri_armour, stat_mental, stat_charge_dist,
//   stat_cost, ownership
//
// STRICTNESS (T0.2): unknown fields are rejected loudly (error carries the key
// and the line number); wrong arity, duplicate fields, and malformed numbers
// are hard errors. Comments ('//') and blank lines are structural only.
//
// CANONICALIZATION RULES (round-trip contract, implemented by
// SerializeCanonical -- the fixture's original formatting is NOT
// reconstructible, so canonical form, byte-identical is over-strict [R11]):
//   1. Fixed field order = the schema order above.
//   2. Values separated by ", " (single space after comma); key and values by
//      one space. No trailing whitespace; LF line endings; blank line between
//      units.
//   3. Comment lines and blank lines are dropped.
//   4. 'type' value kept verbatim (display name, may contain spaces); every
//      other token field lowercased.
//   5. Integers: no leading zeros ('07' -> '7'); '-' preserved.
//   6. Floats: at most 2 decimals, trailing zeros trimmed, bare integer form
//      kept bare ('1.20' -> '1.2', '3.0' -> '3', '.5' -> '0.5').
//   7. Duplicate keys rejected (not deduplicated).
#pragma once

#include "CoreMinimal.h"

/** One EDU field: key + normalized values (values as written post-normalization). */
struct FEduField
{
	FString Key;
	TArray<FString> Values;

	const FString& GetValue() const
	{
		static const FString Empty;
		return Values.Num() > 0 ? Values[0] : Empty;
	}
};

/** One EDU unit block = ordered fields in schema order. */
struct FEduUnit
{
	TArray<FEduField> Fields;

	const FEduField* FindField(const FString& Key) const;
};

/** Parse failure details (logged / asserted by tests; never swallowed). */
struct FEduParseError
{
	bool bFailed = false;
	int32 LineNumber = INDEX_NONE;
	FString Message;
};

struct FBattleEduParser
{
	/** Number of fields in the schema (reality-checked: 14). */
	static int32 SchemaFieldCount();

	/** Canonical field order. */
	static const TArray<FString>& SchemaKeys();

	/** Parse full EDU text into units. Fails loudly on any deviation. */
	static bool Parse(const FString& EduText, TArray<FEduUnit>& OutUnits, FEduParseError& OutError);

	/** Serialize units in canonical form (rules in the header comment). */
	static FString SerializeCanonical(const TArray<FEduUnit>& Units);

	/** Convenience: parse + serialize (the canonicalizer). */
	static bool Canonicalize(const FString& EduText, FString& OutCanonicalText, FEduParseError& OutError);
};
