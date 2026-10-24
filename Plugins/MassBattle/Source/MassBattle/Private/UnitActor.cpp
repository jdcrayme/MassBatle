#include "UnitActor.h"
#include "BattleGameplayTags.h"
#include "BattleSpawnLibrary.h"
#include "BattleRegistry.h"
#include "MassBattleModule.h"

#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassCommandBuffer.h"
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

void AUnitActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Audit low: destroying a unit actor used to leak its registry slot and
	// its Mass entities. Teardown is valid only while the world's Mass entity
	// manager is alive and not mid-processing (GetSubsystem returns null after
	// subsystem deinit, which is the guard for world-teardown ordering).
	Super::EndPlay(EndPlayReason);

	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !EntitySubsystem || !UnitHandle.IsValid())
	{
		return;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (EntityManager.IsProcessing())
	{
		// Structural changes are forbidden mid-processing; the roster entities
		// die with the world in that (not expected on the game thread) case.
		UE_LOG(LogMassBattle, Warning, TEXT("UnitActor %s destroyed while Mass is processing; roster teardown deferred"), *GetName());
		return;
	}
	if (const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(UnitHandle))
	{
		for (const FMassEntityHandle Entity : *Roster)
		{
			EntityManager.Defer().DestroyEntity(Entity);
		}
		EntityManager.FlushCommands();
	}
	Registry->UnregisterUnit(UnitHandle);
	UE_LOG(LogMassBattle, Verbose, TEXT("EXEC_RESULT UnitActorTeardown Actor=%s Unit=%u:%u"),
	       *GetName(), UnitHandle.Index, UnitHandle.Serial);
	UnitHandle = FUnitHandle();
}

void AUnitActor::OnDeathEvent(FGameplayTag EventTag, const FGameplayEventData* Payload)
{
	++DeathEventCount;
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT UnitDiedEvent Actor=%s Count=%d"), *GetName(), DeathEventCount);
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
			// arch §5.1 down-path: (1) write the new value into the registry's
			// cached plain-struct snapshot, (2) set the dirty flag for UnitSync.
			if (FBattleAttributeSnapshot* Cached =
				const_cast<FBattleAttributeSnapshot*>(Registry->FindCachedAttributes(UnitHandle)))
			{
				const FString& AttrName = Data.Attribute.AttributeName;
				if (AttrName == TEXT("Morale")) { Cached->Morale = Data.NewValue; }
				else if (AttrName == TEXT("Fatigue")) { Cached->Fatigue = Data.NewValue; }
				else if (AttrName == TEXT("Experience")) { Cached->Experience = Data.NewValue; }
				else if (AttrName == TEXT("ArmourUpgrade")) { Cached->ArmourUpgrade = Data.NewValue; }
				else if (AttrName == TEXT("MeleeAttackMod")) { Cached->MeleeAttackMod = Data.NewValue; }
				else if (AttrName == TEXT("MeleeDefenseMod")) { Cached->MeleeDefenseMod = Data.NewValue; }
				else if (AttrName == TEXT("RangedAttackMod")) { Cached->RangedAttackMod = Data.NewValue; }
				else if (AttrName == TEXT("SpeedMod")) { Cached->SpeedMod = Data.NewValue; }
				else if (AttrName == TEXT("BlockChanceMod")) { Cached->BlockChanceMod = Data.NewValue; }
				else if (AttrName == TEXT("OrderLatency")) { Cached->OrderLatency = Data.NewValue; }
				else if (AttrName == TEXT("AmmoPool")) { Cached->AmmoPool = Data.NewValue; }
			}
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

			if (!bDeathEventBound && AbilitySystem)
			{
				// Up-path observation (arch §5.2): "Battle.Unit.Died" events land here.
				// Native tag (audit H1): an unregistered tag would silently build an
				// EMPTY filter container, and empty containers match ALL events.
				FGameplayTagContainer Filter;
				Filter.AddTag(TAG_Battle_Unit_Died.GetTag());
				ensureMsgf(Filter.IsEmpty() == false, TEXT("Battle.Unit.Died native tag must be registered (BattleGameplayTags.cpp)"));
				DeathEventHandle = AbilitySystem->AddGameplayEventTagContainerDelegate(
					Filter, FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &AUnitActor::OnDeathEvent));
				bDeathEventBound = true;
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
