#include "BattleEduParser.h"

namespace
{
	// Field kinds drive validation + normalization.
	enum EEduValueKind
	{
		EduName,   // verbatim (trimmed), spaces allowed -- 'type' only
		EduToken,  // single token, lowercased
		EduInt,    // integer, leading zeros stripped
		EduFloat,  // float, <=2 decimals, trailing zeros trimmed
		EduCount   // soldier name (token) followed by ints -- handled by arity table
	};

	struct FEduFieldSpec
	{
		FString Key;
		int32 MinValues;             // exact arity unless MaxValues > MinValues
		int32 MaxValues;             // -1 = unbounded lists (attributes/ownership)
		TArray<EEduValueKind> Kinds; // for fixed arity: one kind per value; cycle for lists
		bool bLowercaseAll;          // lowercase every value (lists)
	};

	// Arity/kind table read straight off Data/EDU/golden_edu_v1.txt.
	const TArray<FEduFieldSpec>& GetSchema()
	{
		static const TArray<FEduFieldSpec> Schema = {
			{TEXT("type"),             1,  1, {EduName},                                                        false},
			{TEXT("dictionary"),       1,  1, {EduName},                                                        false},
			{TEXT("category"),         1,  1, {EduToken},                                                       false},
			{TEXT("class"),            1,  1, {EduToken},                                                       false},
			{TEXT("soldier"),          4,  4, {EduName, EduInt, EduInt, EduInt},                                false},
			{TEXT("attributes"),       0, -1, {EduToken},                                                       true},
			{TEXT("formation"),        6,  6, {EduFloat, EduFloat, EduFloat, EduFloat, EduFloat, EduToken},     false},
			{TEXT("stat_health"),      2,  2, {EduInt, EduInt},                                                 false},
			{TEXT("stat_pri"),        11, 11, {EduInt, EduInt, EduToken, EduInt, EduInt, EduToken, EduToken, EduToken, EduInt, EduFloat, EduFloat}, false},
			{TEXT("stat_sec"),        11, 11, {EduInt, EduInt, EduToken, EduInt, EduInt, EduToken, EduToken, EduToken, EduInt, EduFloat, EduFloat}, false},
			{TEXT("stat_pri_armour"),  4,  4, {EduInt, EduInt, EduInt, EduToken},                               false},
			{TEXT("stat_mental"),      3,  3, {EduInt, EduToken, EduToken},                                     false},
			{TEXT("stat_charge_dist"), 1,  1, {EduInt},                                                         false},
			{TEXT("stat_cost"),        6,  6, {EduInt, EduInt, EduInt, EduInt, EduInt, EduInt},                 false},
			{TEXT("ownership"),        1, -1, {EduToken},                                                       true},
		};
		return Schema;
	}

	const FEduFieldSpec* FindSpec(const FString& Key)
	{
		for (const FEduFieldSpec& Spec : GetSchema())
		{
			if (Spec.Key == Key)
			{
				return &Spec;
			}
		}
		return nullptr;
	}

	bool LooksLikeNumber(const FString& S, const bool bAllowDot)
	{
		if (S.IsEmpty())
		{
			return false;
		}
		int32 i = (S[0] == TEXT('-')) ? 1 : 0;
		bool bAnyDigit = false;
		bool bAnyDot = false;
		for (; i < S.Len(); ++i)
		{
			const TCHAR C = S[i];
			if (FChar::IsDigit(C))
			{
				bAnyDigit = true;
			}
			else if (bAllowDot && C == TEXT('.') && !bAnyDot)
			{
				bAnyDot = true;
			}
			else
			{
				return false;
			}
		}
		return bAnyDigit;
	}

	FString NormalizeInt(const FString& Raw, bool& bOutOk)
	{
		const FString Trimmed = Raw.TrimStartAndEnd();
		if (!LooksLikeNumber(Trimmed, /*bAllowDot*/ false))
		{
			bOutOk = false;
			return Raw;
		}
		bOutOk = true;
		FString Digits = Trimmed;
		const bool bNegative = Digits.StartsWith(TEXT("-"));
		if (bNegative)
		{
			Digits.RightChopInline(1);
		}
	int32 FirstSignificant = 0;
	while (FirstSignificant < Digits.Len() - 1 && Digits[FirstSignificant] == TEXT('0'))
	{
		++FirstSignificant;
	}
	Digits = Digits.Mid(FirstSignificant);
	// "-0" normalizes to "0" (audit low: the sign carried through zero-strip).
	if (!bNegative || Digits == TEXT("0"))
	{
		return Digits;
	}
	return TEXT("-") + Digits;
	}

	FString NormalizeFloat(const FString& Raw, bool& bOutOk)
	{
		const FString Trimmed = Raw.TrimStartAndEnd();
		if (!LooksLikeNumber(Trimmed, /*bAllowDot*/ true))
		{
			bOutOk = false;
			return Raw;
		}
		const double Value = FCString::Atod(*Trimmed);
		FString Out = FString::Printf(TEXT("%.2f"), Value);
		// Trim trailing zeros, then a bare dot (3.00 -> 3. -> 3).
		int32 Cut = Out.Len();
		while (Cut > 1 && Out[Cut - 1] == TEXT('0'))
		{
			--Cut;
		}
		if (Cut > 0 && Out[Cut - 1] == TEXT('.'))
		{
			--Cut;
		}
		bOutOk = true;
		return Out.Left(Cut);
	}

	FString NormalizeValue(const FString& Raw, EEduValueKind Kind, bool& bOutOk)
	{
		switch (Kind)
		{
		case EduName:
			bOutOk = Raw.Len() > 0;
			return Raw.TrimStartAndEnd();
		case EduToken:
		{
			// Tokens are single tokens: internal whitespace is a loud error, not
			// a silent two-word token (audit low).
			const FString Trimmed = Raw.TrimStartAndEnd();
			bOutOk = Trimmed.Len() > 0 && !Trimmed.Contains(TEXT(" "));
			return Trimmed.ToLower();
		}
		case EduInt:
			return NormalizeInt(Raw, bOutOk);
		case EduFloat:
		default:
			return NormalizeFloat(Raw, bOutOk);
		}
	}

	// Split "a, b, c" on commas; whitespace inside tokens collapses to single
	// spaces (EduName only, since tokens must be single tokens).
	TArray<FString> SplitValues(const FString& ValueText)
	{
		TArray<FString> Out;
		FString Current;
		auto Flush = [&Current, &Out]()
		{
			Out.Add(MoveTemp(Current));
			Current.Reset();
		};
		for (int32 i = 0; i < ValueText.Len(); ++i)
		{
			const TCHAR C = ValueText[i];
			if (C == TEXT(','))
			{
				Flush();
			}
			else
			{
				Current += C;
			}
		}
		Flush();
		return Out;
	}

	void Fail(FEduParseError& Err, int32 Line, const FString& Message)
	{
		Err.bFailed = true;
		Err.LineNumber = Line;
		Err.Message = Message;
	}
}

const FEduField* FEduUnit::FindField(const FString& Key) const
{
	for (const FEduField& Field : Fields)
	{
		if (Field.Key == Key)
		{
			return &Field;
		}
	}
	return nullptr;
}

int32 FBattleEduParser::SchemaFieldCount()
{
	return GetSchema().Num();
}

const TArray<FString>& FBattleEduParser::SchemaKeys()
{
	static TArray<FString> Keys;
	static bool bInit = false;
	if (!bInit)
	{
		for (const FEduFieldSpec& Spec : GetSchema())
		{
			Keys.Add(Spec.Key);
		}
		bInit = true;
	}
	return Keys;
}

bool FBattleEduParser::Parse(const FString& EduText, TArray<FEduUnit>& OutUnits, FEduParseError& OutError)
{
	OutUnits.Reset();
	OutError = FEduParseError();

	TArray<FString> Lines;
	EduText.ParseIntoArrayLines(Lines, false);

	bool bSawAnyField = false;
	FEduUnit Current;
	int32 CurrentUnitStartLine = 0; // audit low: order/completeness errors report the unit's first line

	const auto CommitUnit = [&]()
	{
		if (bSawAnyField)
		{
			// Enforce schema order + completeness on the finished unit.
			int32 Cursor = 0;
			for (const FEduFieldSpec& Spec : GetSchema())
			{
				if (Cursor >= Current.Fields.Num() || Current.Fields[Cursor].Key != Spec.Key)
				{
					Fail(OutError, CurrentUnitStartLine, FString::Printf(TEXT("unit missing or misordered field '%s' (canonical order required)"), *Spec.Key));
					return false;
				}
				++Cursor;
			}
			if (Current.Fields.Num() != GetSchema().Num())
			{
				Fail(OutError, CurrentUnitStartLine, TEXT("unit has fields beyond the schema"));
				return false;
			}
			OutUnits.Add(MoveTemp(Current));
			Current = FEduUnit();
			bSawAnyField = false;
		}
		return true;
	};

	for (int32 LineIdx = 0; LineIdx < Lines.Num(); ++LineIdx)
	{
		const int32 LineNumber = LineIdx + 1;
		FString Line = Lines[LineIdx];

		// Comments: strip after '//' and skip if the line becomes empty.
		const int32 CommentAt = Line.Find(TEXT("//"), ESearchCase::CaseSensitive);
		if (CommentAt != INDEX_NONE)
		{
			Line = Line.Left(CommentAt);
		}
		Line = Line.TrimStartAndEnd();
		if (Line.IsEmpty())
		{
			if (!CommitUnit())
			{
				return false;
			}
			continue;
		}

		// "key rest" split on first whitespace run.
		int32 KeyEnd = 0;
		while (KeyEnd < Line.Len() && !FChar::IsWhitespace(Line[KeyEnd]))
		{
			++KeyEnd;
		}
		const FString Key = Line.Left(KeyEnd);
		const bool bNoValues = (KeyEnd == Line.Len());
		FString ValueText = bNoValues ? FString() : Line.Mid(KeyEnd).TrimStartAndEnd();

		const FEduFieldSpec* Spec = FindSpec(Key);
		if (!Spec)
		{
			Fail(OutError, LineNumber, FString::Printf(TEXT("unknown field '%s' (schema has %d fields; see Content/Data/EDU/golden_edu_v1.txt)"), *Key, SchemaFieldCount()));
			return false;
		}
		if (bNoValues && Spec->MinValues > 0)
		{
			Fail(OutError, LineNumber, FString::Printf(TEXT("line has a key but no values: '%s'"), *Line));
			return false;
		}

		// Canonical order check: fields must arrive in schema order within a unit.
		if (Current.Fields.Num() > 0)
		{
			const FString& PrevKey = Current.Fields.Last().Key;
			const int32 PrevIdx = SchemaKeys().Find(PrevKey);
			const int32 CurrIdx = SchemaKeys().Find(Key);
			if (PrevIdx != INDEX_NONE && CurrIdx != INDEX_NONE && CurrIdx <= PrevIdx)
			{
				Fail(OutError, LineNumber, FString::Printf(TEXT("field '%s' out of canonical order (after '%s') or duplicate"), *Key, *PrevKey));
				return false;
			}
		}

		// An empty value list (e.g. a bare "attributes" line) must stay empty --
		// SplitValues would yield one empty-string token.
		TArray<FString> RawValues = bNoValues ? TArray<FString>() : SplitValues(ValueText);
		if (static_cast<int32>(RawValues.Num()) < Spec->MinValues ||
			(Spec->MaxValues >= 0 && static_cast<int32>(RawValues.Num()) > Spec->MaxValues))
		{
			Fail(OutError, LineNumber, FString::Printf(TEXT("field '%s' has %d values, expected %s"), *Key, RawValues.Num(),
				Spec->MaxValues == Spec->MinValues
					? *FString::Printf(TEXT("exactly %d"), Spec->MinValues)
					: *FString::Printf(TEXT("at least %d"), Spec->MinValues)));
			return false;
		}

		FEduField Field;
		Field.Key = Key;
		Field.Values.Reserve(RawValues.Num());
		for (int32 v = 0; v < RawValues.Num(); ++v)
		{
			const EEduValueKind Kind = Spec->Kinds[Spec->bLowercaseAll || Spec->Kinds.Num() == 1 ? 0 : (v % Spec->Kinds.Num())];
			bool bOk = false;
			FString Normalized = NormalizeValue(RawValues[v], Kind, bOk);
			if (!bOk)
			{
				Fail(OutError, LineNumber, FString::Printf(TEXT("field '%s' value %d ('%s') is not a valid %s"),
					*Key, v, *RawValues[v],
					Kind == EduInt ? TEXT("integer") : Kind == EduFloat ? TEXT("number") : TEXT("token")));
				return false;
			}
			Field.Values.Add(MoveTemp(Normalized));
		}
		Current.Fields.Add(MoveTemp(Field));
		if (!bSawAnyField)
		{
			CurrentUnitStartLine = LineNumber;
		}
		bSawAnyField = true;
	}

	if (!CommitUnit())
	{
		return false;
	}

	if (OutUnits.Num() == 0)
	{
		Fail(OutError, Lines.Num(), TEXT("no units found in EDU text"));
		return false;
	}
	return true;
}

FString FBattleEduParser::SerializeCanonical(const TArray<FEduUnit>& Units)
{
	FString Out;
	for (int32 u = 0; u < Units.Num(); ++u)
	{
		if (u > 0)
		{
			Out += TEXT("\n");
		}
		for (const FEduField& Field : Units[u].Fields)
		{
			Out += Field.Key;
			if (Field.Values.Num() == 0)
			{
				Out += TEXT("\n");
				continue;
			}
			Out += TEXT(" ");
			Out += Field.Values[0];
			for (int32 v = 1; v < Field.Values.Num(); ++v)
			{
				Out += TEXT(", ");
				Out += Field.Values[v];
			}
			Out += TEXT("\n");
		}
	}
	return Out;
}

bool FBattleEduParser::Canonicalize(const FString& EduText, FString& OutCanonicalText, FEduParseError& OutError)
{
	TArray<FEduUnit> Units;
	if (!Parse(EduText, Units, OutError))
	{
		return false;
	}
	OutCanonicalText = SerializeCanonical(Units);
	return true;
}
