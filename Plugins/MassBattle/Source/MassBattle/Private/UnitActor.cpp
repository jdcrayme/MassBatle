#include "UnitActor.h"
#include "BattleSpawnLibrary.h"
#include "BattleRegistry.h"
#include "MassBattleModule.h"

#include "Components/ShapeComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

AUnitActor::AUnitActor()
{
	PrimaryActorTick.bCanEverTick = false;

	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AttributeSet = CreateDefaultSubobject<UUnitAttributeSet>(TEXT("AttributeSet"));
	// The ASC must own the set or SetNumericAttributeBase cannot resolve it.
	AbilitySystem->AddAttributeSetSubobject(AttributeSet.Get());
}

void AUnitActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (AbilitySystem)
	{
		// arch §5.1: actor is both owner and avatar (one actor per unit).
		AbilitySystem->InitAbilityActorInfo(this, this);
		// Note: attribute-change delegates are bound lazily in SpawnEntities --
		// bindings made here did not survive to change time on-machine (the
		// harness lambda bound after spawn did), so the registry linkage binds
		// when the registry handle exists anyway.
	}
}

void AUnitActor::OnAttributeChanged(const FOnAttributeChangeData& Data)
{
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT AttrChange Actor=%s Handle=%u:%u Attr=%s Value=%.4f Registry=%d"),
	       *GetName(), UnitHandle.Index, UnitHandle.Serial, *Data.Attribute.AttributeName, Data.NewValue,
	       (GetWorld() && GetWorld()->GetSubsystem<UUnitRegistrySubsystem>()) ? 1 : 0);
	if (UWorld* World = GetWorld())
	{
		if (UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>())
		{
			Registry->MarkStatsDirty(UnitHandle);
		}
	}
}

const TArray<FGameplayAttribute>& UUnitAttributeSet::GetAllAttributes()
{
	static TArray<FGameplayAttribute> Attributes;
	if (Attributes.Num() == 0)
	{
		Attributes = {
			GetMoraleAttribute(),
			GetFatigueAttribute(),
			GetExperienceAttribute(),
			GetArmourUpgradeAttribute(),
			GetMeleeAttackModAttribute(),
			GetMeleeDefenseModAttribute(),
			GetRangedAttackModAttribute(),
			GetSpeedModAttribute(),
			GetBlockChanceModAttribute(),
			GetOrderLatencyAttribute(),
			GetAmmoPoolAttribute(),
		};
	}
	return Attributes;
}

FGameplayAttribute UUnitAttributeSet::FindAttributeByName(const FString& PropertyName)
{
	for (const FGameplayAttribute& Attribute : GetAllAttributes())
	{
		if (Attribute.AttributeName == PropertyName)
		{
			return Attribute;
		}
	}
	return FGameplayAttribute();
}

bool AUnitActor::SpawnEntities(FBattleSpawnResult& OutResult, FString& OutError)
{
	if (UWorld* World = GetWorld())
	{
		// arch §5.1 down-path: attribute changes -> registry dirty flag
		// (UnitSync consumes next frame). Bound once, here, where the handle
		// the handler reports on is about to become meaningful.
		if (!bDelegatesBound && AbilitySystem)
		{
			for (const FGameplayAttribute& Attribute : UUnitAttributeSet::GetAllAttributes())
			{
				AbilitySystem->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this, &AUnitActor::OnAttributeChanged);
			}
			bDelegatesBound = true;
		}

		Spec = FUnitSpawnSpec();
		Spec.EduRowIndex = EduRowIndex;
		Spec.SoldierCount = SoldierCount;
		Spec.UnitName = GetName();

		// Row stats: fixture path (editor-time import path, Phase 0; cook-time
		// commandlet integration is Phase 2 per A2).
		const FString FixturePath = FPaths::ProjectDir() / TEXT("Data/EDU/golden_edu_v1.txt");
		FString EduText;
		if (!FFileHelper::LoadFileToString(EduText, *FixturePath))
		{
			OutError = TEXT("EDU fixture unreadable");
			return false;
		}
		TArray<FEduUnit> Rows;
		FEduParseError ParseErr;
		if (!FBattleEduParser::Parse(EduText, Rows, ParseErr))
		{
			OutError = FString::Printf(TEXT("EDU parse failed: %s"), *ParseErr.Message);
			return false;
		}
		if (!Rows.IsValidIndex(EduRowIndex))
		{
			OutError = FString::Printf(TEXT("EduRowIndex %d out of range (%d rows)"), EduRowIndex, Rows.Num());
			return false;
		}
		const FBattleRowStats* Cached = RowStatsCache.Find(EduRowIndex);
		if (!Cached)
		{
			FBattleRowStats Mapped;
			if (!FBattleRowStats::FromEdu(Rows[EduRowIndex], Mapped, OutError))
			{
				return false;
			}
			Cached = &RowStatsCache.Add(EduRowIndex, Mapped);
		}

		const FBattleSpawnResult Result = UBattleSpawnLibrary::SpawnUnit(World, Spec, *Cached);
		UnitHandle = Result.Unit;
		OutResult = Result;
		return Result.Unit.IsValid();
	}
	OutError = TEXT("no world");
	return false;
}
