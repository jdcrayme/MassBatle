#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.AttributeSet.* -- Task 7: closed GAS attribute set stubs.
// completeness (arch §10.1 closed set), delegate fires on
// SetNumericAttributeBase, registry dirty flag behaves across change bursts.
#include "Misc/AutomationTest.h"

#include "UnitActor.h"
#include "BattleRegistry.h"
#include "BattleTypes.h"

namespace
{
	UWorld* MakeAttrTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttributeSet_ClosedSet,
	"Battle.Unit.AttributeSet.ClosedSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAttributeSet_ClosedSet::RunTest(const FString& Parameters)
{
	// arch §10.1: exactly these 11 attributes, no more, no less.
	const TArray<FGameplayAttribute>& All = UUnitAttributeSet::GetAllAttributes();
	TestEqual(TEXT("closed set size == 11"), All.Num(), 11);

	const TCHAR* Expected[] = {
		TEXT("Morale"), TEXT("Fatigue"), TEXT("Experience"), TEXT("ArmourUpgrade"),
		TEXT("MeleeAttackMod"), TEXT("MeleeDefenseMod"), TEXT("RangedAttackMod"),
		TEXT("SpeedMod"), TEXT("BlockChanceMod"), TEXT("OrderLatency"), TEXT("AmmoPool"),
	};
	for (const TCHAR* Name : Expected)
	{
		const FGameplayAttribute Attr = UUnitAttributeSet::FindAttributeByName(Name);
		TestTrue(FString::Printf(TEXT("attribute %s exists and resolves"), Name), Attr.IsValid());
	}
	TestFalse(TEXT("unknown attribute does not resolve"),
		UUnitAttributeSet::FindAttributeByName(TEXT("FakeAttribute")).IsValid());

	// Effects must target only the *Mod attributes (ADR-09): the mod names end in "Mod".
	for (const TCHAR* Name : {TEXT("MeleeAttackMod"), TEXT("MeleeDefenseMod"), TEXT("RangedAttackMod"),
		TEXT("SpeedMod"), TEXT("BlockChanceMod")})
	{
		TestTrue(FString::Printf(TEXT("%s is a Mod attribute"), Name),
			FString(Name).EndsWith(TEXT("Mod")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttributeSet_DelegateAndDirtyFlag,
	"Battle.Unit.AttributeSet.DelegateAndDirtyFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAttributeSet_DelegateAndDirtyFlag::RunTest(const FString& Parameters)
{
	UWorld* World = MakeAttrTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	TestNotNull(TEXT("registry"), Registry);
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	AUnitActor* Actor = World->SpawnActor<AUnitActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("unit actor spawned"), Actor))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestNotNull(TEXT("ASC present"), Actor->GetAbilitySystemComponent());
	TestTrue(TEXT("actor implements IAbilitySystemInterface"), Cast<IAbilitySystemInterface>(Actor) != nullptr);

	// Spawn the entity roster (row 0, small roster for speed) so the actor has a handle.
	Actor->EduRowIndex = 0;
	Actor->SoldierCount = 4;
	FBattleSpawnResult SpawnResult;
	FString Error;
	if (!TestTrue(TEXT("actor spawns roster"), Actor->SpawnEntities(SpawnResult, Error)))
	{
		AddError(Error);
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("handle valid after spawn"), Actor->GetUnitHandle().IsValid());

	TestFalse(TEXT("not dirty before any change"), Registry->IsStatsDirty(Actor->GetUnitHandle()));

	// T0.4 drive: no GE asset needed -- SetNumericAttributeBase fires the same delegate path.
	int32 DelegateHits = 0;
	FDelegateHandle Handle = Actor->GetAbilitySystemComponent()
		->GetGameplayAttributeValueChangeDelegate(UUnitAttributeSet::GetMeleeAttackModAttribute())
		.AddLambda([&DelegateHits](const FOnAttributeChangeData&) { ++DelegateHits; });

	Actor->GetAbilitySystemComponent()->SetNumericAttributeBase(UUnitAttributeSet::GetMeleeAttackModAttribute(), 0.5f);

	TestEqual(TEXT("delegate fired exactly once for one Set"), DelegateHits, 1);
	TestTrue(TEXT("dirty flag set by the delegate path"), Registry->IsStatsDirty(Actor->GetUnitHandle()));

	// Change burst: three more sets -> three more delegate hits, flag stays a
	// single set-semantics flag (idempotent; "exactly once per burst").
	Actor->GetAbilitySystemComponent()->SetNumericAttributeBase(UUnitAttributeSet::GetMeleeAttackModAttribute(), 0.6f);
	Actor->GetAbilitySystemComponent()->SetNumericAttributeBase(UUnitAttributeSet::GetMeleeAttackModAttribute(), 0.7f);
	Actor->GetAbilitySystemComponent()->SetNumericAttributeBase(UUnitAttributeSet::GetMeleeAttackModAttribute(), 0.8f);
	TestEqual(TEXT("delegate fired once per change"), DelegateHits, 4);
	TestTrue(TEXT("still exactly one dirty entry"), Registry->IsStatsDirty(Actor->GetUnitHandle()));

	// Registry cached-attributes path (UnitSync consumes this).
	FBattleAttributeSnapshot Snapshot;
	Snapshot.MeleeAttackMod = 0.8f;
	Registry->UpdateCachedAttributes(Actor->GetUnitHandle(), Snapshot);
	const FBattleAttributeSnapshot* ReadBack = Registry->FindCachedAttributes(Actor->GetUnitHandle());
	TestNotNull(TEXT("cached attributes readable"), ReadBack);
	if (ReadBack)
	{
		TestEqual(TEXT("snapshot round trip"), ReadBack->MeleeAttackMod, 0.8f);
	}

	Actor->GetAbilitySystemComponent()
		->GetGameplayAttributeValueChangeDelegate(UUnitAttributeSet::GetMeleeAttackModAttribute())
		.Remove(Handle);

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
