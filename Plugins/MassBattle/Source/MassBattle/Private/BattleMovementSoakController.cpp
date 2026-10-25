#include "BattleMovementSoakController.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleRegistry.h"
#include "BattleSpawnLibrary.h"
#include "BattleEduParser.h"
#include "BattleRngRegistry.h"
#include "BattleDebugSubsystem.h"
#include "BattleTestSettings.h"
#include "BattleTypes.h"

#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "MassCommonFragments.h"
#include "GauntletModule.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TArray<FEduUnit> LoadFixture()
	{
		TArray<FEduUnit> Rows;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FEduParseError Err;
			FBattleEduParser::Parse(Text, Rows, Err);
		}
		return Rows;
	}
}

void UBattleMovementSoakController::OnInit()
{
	Super::OnInit();

	// A4 guard rail: the test contract must apply on the staged -game boot --
	// fixed-step + full-detail assertions must hold here or the soak is vacuous.
	UBattleTestSettings::ApplyTestMode();
	bTestModeOk = UBattleTestSettings::AssertTestMode();
	UE_LOG(LogGauntlet, Display, TEXT("GAUNTLET BattleMovementSoak OnInit testMode=%d fixedStep=%d fixedDt=%.4f"),
		bTestModeOk ? 1 : 0, FApp::UseFixedTimeStep() ? 1 : 0, FApp::GetFixedDeltaTime());

	// Seeded determinism: one battle root; the movement stream derives from it.
	if (UWorld* World = GetWorld())
	{
		if (UBattleRngSubsystem* Rng = World->GetSubsystem<UBattleRngSubsystem>())
		{
			Rng->Seed(20261025);
		}
	}
}

void UBattleMovementSoakController::OnTick(float TimeDelta)
{
	if (Phase == EPhase::Done)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	PhaseTime += TimeDelta;

	if (Phase == EPhase::WaitForWorld)
	{
		// Wait for begin play + the first Mass ticks before structural work.
		if (World->HasBegunPlay() && !World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().IsProcessing())
		{
			++SpawnTicks;
			if (SpawnTicks > 10)
			{
				Phase = EPhase::Spawn;
				PhaseTime = 0.f;
			}
		}
		return;
	}

	if (Phase == EPhase::Spawn)
	{
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>();
		UBattleRngSubsystem* Rng = World->GetSubsystem<UBattleRngSubsystem>();
		if (!Registry || !ES || !Rng)
		{
			FinishTest(1);
			return;
		}
		if (ES->GetMutableEntityManager().IsProcessing())
		{
			return; // never create structures mid-processing
		}

		const TArray<FEduUnit> Rows = LoadFixture();
		if (Rows.Num() < 1)
		{
			UE_LOG(LogGauntlet, Error, TEXT("GAUNTLET BattleMovementSoak EDU fixture unreadable"));
			FinishTest(1);
			return;
		}

		// 8 units in a 4x2 grid, 30 m columns / 30 m rows, facing north.
		for (int32 i = 0; i < 8; ++i)
		{
			const int32 Col = i % 4;
			const int32 Row = i / 4;
			FUnitSpawnSpec Spec;
			Spec.EduRowIndex = i % Rows.Num();
			Spec.SoldierCount = 40;
			Spec.UnitName = FString::Printf(TEXT("Soak_%d"), i);
			Spec.SpawnPos = FVector2D(-4500.f + Col * 3000.f, Row * 3000.f);
			Spec.SpawnYaw = 90.f;
			FBattleSpawnResult Result;
			FString Error;
			if (!UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[Spec.EduRowIndex], Result, Error) || !Result.Unit.IsValid())
			{
				UE_LOG(LogGauntlet, Error, TEXT("GAUNTLET BattleMovementSoak spawn %d failed: %s"), i, *Error);
				FinishTest(1);
				return;
			}
		}

		ArchetypeCountAtSpawn = ES->GetMutableEntityManager().DebugGetArchetypesCount();
		EscalationsAtSpawn = 0;
		if (UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>())
		{
			Debug->ResetDiagnostics();
		}
		UE_LOG(LogGauntlet, Display, TEXT("GAUNTLET BattleMovementSoak Spawned Units=8 Archetypes=%d"),
			ArchetypeCountAtSpawn);
		Phase = EPhase::Soak;
		PhaseTime = 0.f;
		SoakTime = 0.f;
		return;
	}

	if (Phase == EPhase::Soak)
	{
		SoakTime += TimeDelta;
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UBattleRngSubsystem* Rng = World->GetSubsystem<UBattleRngSubsystem>();
		if (!Registry || !Rng)
		{
			FinishTest(1);
			return;
		}
		FRandomStream& Stream = Rng->GetStream(TEXT("Soak.Movement"));

		// Seeded random orders every ~10 s through the A2 API.
		if (SoakTime >= NextOrderTime)
		{
			NextOrderTime += OrderIntervalSeconds;
			const TArray<FUnitHandle> Units = Registry->GetSortedHandles();
			for (const FUnitHandle& Unit : Units)
			{
				if (Stream.FRand() < 0.2f)
				{
					Registry->IssueStopOrder(Unit);
					continue;
				}
				// Goals inside a 60x60 m box around the spawn area: inside the
				// cost grid, so paths always exist.
				const FVector2D Goal(Stream.FRandRange(-3000.f, 3000.f), Stream.FRandRange(-3000.f, 4500.f));
				Registry->IssueMoveOrder(Unit, Goal, Stream.FRandRange(0.f, 360.f));
			}
		}

		// Sample every ~5 s: separation (no co-located pairs) + no entity
		// outside its unit body (separation events, roadmap T1.8 "separation = 0").
		if (SoakTime >= NextSampleTime)
		{
			NextSampleTime += SampleIntervalSeconds;
			++Samples;
			UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>();
			FMassEntityManager& EM = ES->GetMutableEntityManager();
			for (const FUnitHandle& Unit : Registry->GetSortedHandles())
			{
				const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Unit);
				const FMassEntityHandle* Head = (Roster && Roster->Num() > 0) ? &(*Roster)[0] : nullptr;
				const FUnitFormationSharedFragment* Formation =
					Head ? EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>(*Head) : nullptr;
				if (!Roster || !Formation)
				{
					continue;
				}
				const int32 NumRanks = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Formation->NumSlots) / Formation->NumFiles));
				const float Rad = FMath::DegreesToRadians(Formation->FacingYaw);
				const FVector2D Front(FMath::Cos(Rad), FMath::Sin(Rad));
				const float HalfRanksM = static_cast<float>(NumRanks - 1) * Formation->RankSpacing * 0.5f * 0.01f;
				const FVector2D BodyCenter = Formation->AnchorPos * 0.01f - Front * HalfRanksM;
				const float LimitAlong = HalfRanksM + Formation->RankSpacing * 0.5f * 0.01f + 5.f;
				const float LimitSide = (Formation->NumFiles * Formation->FileSpacing * 0.5f) * 0.01f + 5.f;

				float MinSq = FLT_MAX;
				for (int32 i = 0; i < Roster->Num(); ++i)
				{
					const FTransformFragment* T = FMassEntityView(EM, (*Roster)[i]).GetFragmentDataPtr<FTransformFragment>();
					if (!T)
					{
						continue;
					}
					const FVector2D P(T->GetTransform().GetTranslation().X * 0.01f, T->GetTransform().GetTranslation().Y * 0.01f);
					const FVector2D Delta = P - BodyCenter;
					if (FMath::Abs(FVector2D::DotProduct(Delta, Front)) > LimitAlong
						|| FMath::Abs(FVector2D::DotProduct(Delta, FVector2D(-Front.Y, Front.X))) > LimitSide)
					{
						++SeparationViolations;
					}
					for (int32 j = i + 1; j < Roster->Num(); ++j)
					{
						const FTransformFragment* T2 = FMassEntityView(EM, (*Roster)[j]).GetFragmentDataPtr<FTransformFragment>();
						if (T2)
						{
							MinSq = FMath::Min(MinSq, FVector2D::DistSquared(P,
								FVector2D(T2->GetTransform().GetTranslation().X * 0.01f, T2->GetTransform().GetTranslation().Y * 0.01f)));
						}
					}
				}
				if (FMath::Sqrt(MinSq) < 0.15f)
				{
					++SeparationViolations;
				}
			}
			// Progress evidence every ~60 s (12 samples), not per sample.
			if (Samples % 12 == 0)
			{
				UE_LOG(LogGauntlet, Display, TEXT("GAUNTLET BattleMovementSoak progress t=%.0fs violations=%d"),
					SoakTime, SeparationViolations);
			}
		}

		if (SoakTime >= SoakDurationSeconds)
		{
			// T1.8 verbatim: stuck = 0, separation = 0, archetype count constant.
			const int32 Escalations = World->GetSubsystem<UBattleDebugSubsystem>()
				? World->GetSubsystem<UBattleDebugSubsystem>()->GetLadderEscalations() : -1;
			const int32 ArchetypesNow = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().DebugGetArchetypesCount();
			const bool bOk = bTestModeOk
				&& Escalations - EscalationsAtSpawn == 0
				&& SeparationViolations == 0
				&& ArchetypesNow == ArchetypeCountAtSpawn;
			UE_LOG(LogGauntlet, Display,
				TEXT("GAUNTLET BattleMovementSoak FINAL t=%.0fs stuck=%d separationViolations=%d archetypes=%d (spawn %d) testMode=%d ok=%d"),
				SoakTime, Escalations - EscalationsAtSpawn, SeparationViolations, ArchetypesNow, ArchetypeCountAtSpawn,
				bTestModeOk ? 1 : 0, bOk ? 1 : 0);
			Phase = EPhase::Done;
			FinishTest(bOk ? 0 : 1);
		}
	}
}

void UBattleMovementSoakController::FinishTest(int32 ExitCode)
{
	UE_LOG(LogGauntlet, Display, TEXT("GAUNTLET BattleMovementSoak END exit=%d"), ExitCode);
	EndTest(ExitCode);
}

UBattleMovementSoakController::~UBattleMovementSoakController()
{
}
