// DemoUnitVisualSubsystem.cpp -- see header.
#include "DemoUnitVisualSubsystem.h"
#include "BattleRegistry.h"
#include "BattleMovementFragments.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/CollisionProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "MassCommonFragments.h"
#include "MeshDescription.h"
#include "Misc/App.h"
#include "StaticMeshAttributes.h"
#include "Misc/Paths.h"

namespace
{
	// Capsule proportions (cm): a soldier ~1.8 m tall on a 1.2 m spacing.
	constexpr float CapsuleRadiusCm = 35.f;
	constexpr float CapsuleCylCm = 110.f; // cylinder part; total height = 2R + Cyl = 180

	FLinearColor TeamColor(EDemoTeam Team)
	{
		return Team == EDemoTeam::Player ? FLinearColor(0.16f, 0.42f, 1.f) : FLinearColor(1.f, 0.22f, 0.14f);
	}
}

// -- USubsystem / tickable ---------------------------------------------------

void UDemoUnitVisualSubsystem::Deinitialize()
{
	Units.Reset();
	TeamByUnit.Reset();
	VisualByUnit.Reset();
	Selected.Reset();
	Super::Deinitialize();
}

TStatId UDemoUnitVisualSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDemoUnitVisualSubsystem, STATGROUP_Tickables);
}

bool UDemoUnitVisualSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

// -- registration ------------------------------------------------------------

void UDemoUnitVisualSubsystem::RegisterUnit(FUnitHandle Handle, EDemoTeam Team)
{
	if (!Handle.IsValid() || TeamByUnit.Contains(Handle))
	{
		return;
	}
	TeamByUnit.Add(Handle, Team);
	Units.Add(Handle);
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT VisualRegister Unit=%u:%u Team=%d"),
		Handle.Index, Handle.Serial, static_cast<int32>(Team));
}

void UDemoUnitVisualSubsystem::GetUnitsOfTeam(EDemoTeam Team, TArray<FUnitHandle>& OutHandles) const
{
	OutHandles.Reset();
	for (const FUnitHandle& Handle : Units)
	{
		if (TeamByUnit.FindRef(Handle) == Team)
		{
			OutHandles.Add(Handle);
		}
	}
}

int32 UDemoUnitVisualSubsystem::AdoptUnknownRegistryUnits(UUnitRegistrySubsystem* Registry)
{
	if (!Registry)
	{
		return 0;
	}
	int32 Adopted = 0;
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		if (TeamByUnit.Contains(Handle))
		{
			continue;
		}
		// Demo team convention: the SIDE the unit fights on (TeamId; team 0
		// = the Player convention) drives the faction split -- EDU row order
		// is fixture data, never a faction notion (audit B-3).
		EDemoTeam Team = EDemoTeam::Enemy;
		if (const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(Handle);
			Spec && Spec->TeamId == 0)
		{
			Team = EDemoTeam::Player;
		}
		RegisterUnit(Handle, Team);
		++Adopted;
	}
	return Adopted;
}

EDemoTeam UDemoUnitVisualSubsystem::GetTeam(FUnitHandle Handle) const
{
	return TeamByUnit.FindRef(Handle);
}

void UDemoUnitVisualSubsystem::SetSelected(FUnitHandle Handle, bool bSelected)
{
	if (bSelected != Selected.Contains(Handle))
	{
		if (bSelected)
		{
			Selected.Add(Handle);
		}
		else
		{
			Selected.Remove(Handle);
		}
		bSelectedDirty = true;
	}
}

bool UDemoUnitVisualSubsystem::IsSelected(FUnitHandle Handle) const
{
	return Selected.Contains(Handle);
}

// -- per-frame sync ----------------------------------------------------------

void UDemoUnitVisualSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* Mass = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !Mass)
	{
		return;
	}

	// Prune stale handles (unit destroyed / roster released).
	for (int32 i = Units.Num() - 1; i >= 0; --i)
	{
		if (!Registry->IsHandleValid(Units[i]))
		{
			if (TObjectPtr<UInstancedStaticMeshComponent>* Visual = VisualByUnit.Find(Units[i]))
			{
				if (*Visual != nullptr)
				{
					if (AActor* Host = (*Visual)->GetOwner())
					{
						Host->Destroy();
					}
				}
			}
			VisualByUnit.Remove(Units[i]);
			Selected.Remove(Units[i]);
			TeamByUnit.Remove(Units[i]);
			CentroidByUnit.Remove(Units[i]);
			Units.RemoveAtSwap(i);
		}
	}

	if (bSelectedDirty)
	{
		for (const FUnitHandle& Handle : Units)
		{
			UpdateSelectionTint(Handle);
		}
		bSelectedDirty = false;
	}

	FMassEntityManager& EM = Mass->GetMutableEntityManager();

	auto GatherRosterTransforms = [&EM](const TArray<FMassEntityHandle>& Roster, TArray<FTransform>& Out)
	{
		Out.Reset(Roster.Num());
		for (const FMassEntityHandle& Entity : Roster)
		{
			FTransform T(FTransform::Identity);
			if (const FTransformFragment* Frag = FMassEntityView(EM, Entity).GetFragmentDataPtr<FTransformFragment>())
			{
				T = Frag->GetTransform();
			}
			Out.Add(T);
		}
	};

	for (const FUnitHandle& Handle : Units)
	{
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		if (!Roster || Roster->Num() == 0)
		{
			continue; // spawn not run yet / roster empty
		}
		UInstancedStaticMeshComponent* Visual = EnsureVisual(Handle, TeamByUnit.FindRef(Handle));
		if (!Visual)
		{
			continue;
		}

		// Roster count changes (spawn fixup / casualties) rebuild; the common
		// path is a batched in-place update.
		TArray<FTransform> Instances;
		GatherRosterTransforms(*Roster, Instances);
		if (Visual->GetInstanceCount() != Roster->Num())
		{
			Visual->ClearInstances();
			Visual->AddInstances(Instances, /*bShouldReturnIndices*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		}
		else
		{
			Visual->BatchUpdateInstancesTransforms(/*StartInstanceIndex*/ 0, Instances,
				/*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		}

		// PR-2 shoring (the owner report: entities outrun the banner): the
		// visual blob center -- mean of the living entities, drop the farthest
		// quarter, re-mean (the 25% trim keeps a stuck straggler from dragging
		// the card off the formation). The BANNERS track this; the SIM's
		// formation anchor (BodyCenterCm) stays the order/sim surface.
		if (Instances.Num() > 0)
		{
			FVector2D Mean = FVector2D::ZeroVector;
			for (const FTransform& T : Instances)
			{
				Mean += FVector2D(T.GetTranslation());
			}
			Mean /= static_cast<float>(Instances.Num());
			if (Instances.Num() >= 8)
			{
				Instances.Sort([Mean](const FTransform& A, const FTransform& B)
				{
					return FVector2D::DistSquared(FVector2D(A.GetTranslation()), Mean)
						> FVector2D::DistSquared(FVector2D(B.GetTranslation()), Mean);
				});
				const int32 Keep = FMath::Max(1, Instances.Num() * 3 / 4);
				FVector2D Trimmed = FVector2D::ZeroVector;
				for (int32 i = Instances.Num() - Keep; i < Instances.Num(); ++i)
				{
					Trimmed += FVector2D(Instances[i].GetTranslation());
				}
				Mean = Trimmed / static_cast<float>(Keep);
			}
			CentroidByUnit.Add(Handle, Mean);
		}
	}

	// -- PR-2 banner rects: ONE projection pass per tick, consumed by the HUD
	// (draw) and the player controller (click hit tests). The zoom-LOD
	// collapse is measured in screen space: the projected frontage narrower
	// than the expanded card collapses to the small flag (M2TW convention).
	// The banners project the TRIMMED ENTITY CENTROID (the visual blob), not
	// the sim's formation anchor -- the card sits on the fight.
	// PR-5 tuning: the collapse carries HYSTERESIS (collapse below the
	// threshold, re-expand above +20%) -- the projected frontage jitters
	// tick-to-tick at the boundary (the blob breathes) and without the gap
	// the card flickers between states while zooming.
	PrevBannerCollapsed = MoveTemp(BannerCollapsed);
	BannerRects.Reset();
	BannerCollapsed.Reset();
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	for (const FUnitHandle& Handle : Units)
	{
		FDemoUnitBodyInfo Info;
		if (!GetUnitBodyInfo(Handle, Info))
		{
			continue;
		}
		const FVector2D ProjCm = CentroidByUnit.FindRef(Handle).IsNearlyZero()
			? Info.BodyCenterCm
			: CentroidByUnit.FindRef(Handle);
		const FVector Center(ProjCm.X, ProjCm.Y, DemoUi::BodyLabelZCm);
		FVector2D Screen;
		if (!PC->ProjectWorldLocationToScreen(Center, Screen))
		{
			continue;
		}
		// Projected frontage (the facing's right vector endpoints).
		const float Rad = FMath::DegreesToRadians(Info.FacingYawDeg);
		const FVector2D Right(-FMath::Sin(Rad), FMath::Cos(Rad));
		const float HalfFrontCm = Info.NumFiles * Info.FileSpacingM * 50.f;
		FVector2D EdgeL, EdgeR;
		const bool bEdges = PC->ProjectWorldLocationToScreen(
			FVector(ProjCm.X - Right.X * HalfFrontCm, ProjCm.Y - Right.Y * HalfFrontCm, DemoUi::BodyLabelZCm), EdgeL)
			&& PC->ProjectWorldLocationToScreen(
			FVector(ProjCm.X + Right.X * HalfFrontCm, ProjCm.Y + Right.Y * HalfFrontCm, DemoUi::BodyLabelZCm), EdgeR);
		const float FrontPx = bEdges ? FVector2D::Distance(EdgeL, EdgeR) : 0.f;

		// The expanded card is 96 x 30 px; the collapsed flag 14 x 20.
		// PR-5: collapse below CollapsePx, re-expand only above ExpandPx
		// (+20% hysteresis -- the frontage jitters at the boundary).
		constexpr float CardW = 96.f, CardH = 30.f, FlagW = 14.f, FlagH = 20.f;
		constexpr float CollapsePx = CardW * 1.5f;
		constexpr float ExpandPx = CollapsePx * 1.2f;
		const bool bWasCollapsed = PrevBannerCollapsed.Contains(Handle);
		const bool bCollapsed = bEdges
			&& FrontPx < (bWasCollapsed ? ExpandPx : CollapsePx);
		const float W = bCollapsed ? FlagW : CardW;
		const float H = bCollapsed ? FlagH : CardH;
		// Anchored ABOVE the body center (a 6 px gap for the selection ring).
		BannerRects.Add(Handle, FBox2D(Screen + FVector2D(-W * 0.5f, -(H + 6.f)), Screen + FVector2D(W * 0.5f, -6.f)));
		if (bCollapsed)
		{
			BannerCollapsed.Add(Handle);
		}
	}
}

bool UDemoUnitVisualSubsystem::GetBannerRect(FUnitHandle Handle, FBox2D& OutRect, bool& bOutCollapsed) const
{
	const FBox2D* Rect = BannerRects.Find(Handle);
	if (!Rect)
	{
		return false;
	}
	OutRect = *Rect;
	bOutCollapsed = BannerCollapsed.Contains(Handle);
	return true;
}

bool UDemoUnitVisualSubsystem::FindBannerAtScreen(const FVector2D& ScreenPos, FUnitHandle& OutUnit) const
{
	// Registration order reversed: later units draw on top, so they pick first.
	for (int32 i = Units.Num() - 1; i >= 0; --i)
	{
		if (const FBox2D* Rect = BannerRects.Find(Units[i]))
		{
			if (Rect->IsInside(ScreenPos))
			{
				OutUnit = Units[i];
				return true;
			}
		}
	}
	return false;
}

UInstancedStaticMeshComponent* UDemoUnitVisualSubsystem::EnsureVisual(FUnitHandle Handle, EDemoTeam Team)
{
	if (TObjectPtr<UInstancedStaticMeshComponent>* Existing = VisualByUnit.Find(Handle))
	{
		return Existing->Get();
	}
	// Cooked + no-RHI (the nightly's staged -nullrhi run) cannot create render
	// resources for a runtime-built static mesh at all (StaticMesh.cpp:2810
	// asserts FApp::CanEverRender || !RequiresCookedData). Nothing renders in
	// that environment by design -- skip visuals instead of crashing.
	if (FPlatformProperties::RequiresCookedData() && !FApp::CanEverRender())
	{
		return nullptr;
	}
	UStaticMesh* Mesh = EnsureCapsuleMesh();
	if (!Mesh)
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Components need an actor owner with a world (subsystem-owned components
	// fail RegisterComponent's MyOwnerWorld ensure). One transient host actor
	// per unit; the ISM is its root.
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	AActor* Host = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!Host)
	{
		return nullptr;
	}

	UInstancedStaticMeshComponent* Visual = NewObject<UInstancedStaticMeshComponent>(Host);
	Visual->SetStaticMesh(Mesh);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCastShadow(false);
	Visual->SetGenerateOverlapEvents(false);
	Visual->Mobility = EComponentMobility::Movable;
	Host->SetRootComponent(Visual);
	Visual->RegisterComponent();

	// Faction tint + selection highlight through one MID per unit (engine
	// BasicShapeMaterial carries the "Color" vector parameter).
	if (UMaterialInterface* BaseMat = Mesh->GetMaterial(0))
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, this);
		MID->SetVectorParameterValue(TEXT("Color"), TeamColor(Team));
		Visual->SetMaterial(0, MID);
	}

	VisualByUnit.Add(Handle, Visual);
	UpdateSelectionTint(Handle);
	return Visual;
}

void UDemoUnitVisualSubsystem::UpdateSelectionTint(FUnitHandle Handle)
{
	if (TObjectPtr<UInstancedStaticMeshComponent>* VisualPtr = VisualByUnit.Find(Handle))
	{
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>((*VisualPtr)->GetMaterial(0)))
		{
			const EDemoTeam Team = TeamByUnit.FindRef(Handle);
			FLinearColor Color = TeamColor(Team);
			if (Selected.Contains(Handle))
			{
				Color = FLinearColor::LerpUsingHSV(Color, FLinearColor::Green, 0.75f) * 1.6f;
			}
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}

// -- runtime capsule mesh ----------------------------------------------------

UStaticMesh* UDemoUnitVisualSubsystem::EnsureCapsuleMesh()
{
	if (CapsuleMesh)
	{
		return CapsuleMesh.Get();
	}

	// Lathe of a capsule silhouette around +Z. Profile goes bottom pole ->
	// bottom hemisphere -> cylinder top -> top hemisphere -> top pole; origin
	// at the BASE (z = 0) so instance transforms map entity planar transforms
	// straight to standing capsules.
	struct FProfilePoint { float R; float Z; float NR; float NZ; };
	TArray<FProfilePoint> Profile;
	constexpr int32 ArcSegments = 6;
	for (int32 i = 0; i <= ArcSegments; ++i) // bottom hemisphere: -90..0 deg
	{
		const float A = FMath::DegreesToRadians(-90.f + 90.f * i / ArcSegments);
		Profile.Push({ CapsuleRadiusCm * FMath::Cos(A), CapsuleRadiusCm + CapsuleRadiusCm * FMath::Sin(A),
			FMath::Cos(A), FMath::Sin(A) });
	}
	Profile.Push({ CapsuleRadiusCm, CapsuleRadiusCm + CapsuleCylCm, 1.f, 0.f }); // cylinder ring
	for (int32 i = 1; i <= ArcSegments; ++i) // top hemisphere: 0..90 deg
	{
		const float A = FMath::DegreesToRadians(90.f * i / ArcSegments);
		Profile.Push({ CapsuleRadiusCm * FMath::Cos(A), CapsuleRadiusCm + CapsuleCylCm + CapsuleRadiusCm * FMath::Sin(A),
			FMath::Cos(A), FMath::Sin(A) });
	}

	FMeshDescription Desc;
	FStaticMeshAttributes Attr(Desc);
	Attr.Register();
	Attr.GetVertexInstanceUVs().SetNumChannels(1);

	const FPolygonGroupID Group = Desc.CreatePolygonGroup(); // material slot 0

	constexpr int32 RadialSegments = 16;
	const int32 P = Profile.Num();
	TArray<TArray<FVertexInstanceID>> Inst; // [profile][radial+1 seam]
	Inst.SetNum(P);
	for (int32 T = 0; T < P; ++T)
	{
		Inst[T].SetNum(RadialSegments + 1);
		for (int32 J = 0; J <= RadialSegments; ++J)
		{
			const float Theta = FMath::DegreesToRadians(360.f * J / RadialSegments);
			const float Cos = FMath::Cos(Theta);
			const float Sin = FMath::Sin(Theta);

			const FVertexID V = Desc.CreateVertex();
			Attr.GetVertexPositions()[V] = FVector3f(Profile[T].R * Cos, Profile[T].R * Sin, Profile[T].Z);

			const FVertexInstanceID I = Desc.CreateVertexInstance(V);
			// Surface normal from the 2D profile normal revolved around +Z.
			Attr.GetVertexInstanceNormals()[I] = FVector3f(Profile[T].NR * Cos, Profile[T].NR * Sin, Profile[T].NZ);
			// Tangent along the revolution direction dP/dTheta.
			Attr.GetVertexInstanceTangents()[I] = FVector3f(-Sin, Cos, 0.f);
			Attr.GetVertexInstanceBinormalSigns()[I] = 1.f;
			Attr.GetVertexInstanceUVs().Set(I, /*Channel*/ 0,
				FVector2f(J / static_cast<float>(RadialSegments), T / static_cast<float>(P - 1)));
			Inst[T][J] = I;
		}
	}

	for (int32 T = 0; T < P - 1; ++T)
	{
		for (int32 J = 0; J < RadialSegments; ++J)
		{
			// Outward winding (CCW from outside): verified on the +X seam.
			// UE 5.8: vertex IDs derive from the instances; no vertex array arg.
			Desc.CreateTriangle(Group,
				{ Inst[T][J], Inst[T][J + 1], Inst[T + 1][J + 1] });
			Desc.CreateTriangle(Group,
				{ Inst[T][J], Inst[T + 1][J + 1], Inst[T + 1][J] });
		}
	}

	UObject* Outer = GetOuter() ? GetOuter() : this;
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
	Mesh->NeverStream = true;

	if (UMaterialInterface* BaseMat = Cast<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")).TryLoad()))
	{
		Mesh->GetStaticMaterials().Add(FStaticMaterial(BaseMat));
	}

	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&Desc);
	// Cooked builds can only fast-build runtime meshes (UStaticMesh::
	// BuildFromMeshDescriptions asserts !WITH_EDITOR && !bFastBuild) -- the
	// nightly staged run surfaced this; bFastBuild is editor-harmless.
	// FBuildMeshDescriptionsParams is nested in UStaticMesh (5.8).
	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bFastBuild = true;
	Mesh->BuildFromMeshDescriptions(Descriptions, BuildParams);

	CapsuleMesh = Mesh;
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoCapsuleMesh built (r=%.0fcm h=%.0fcm)"),
		CapsuleRadiusCm, 2.f * CapsuleRadiusCm + CapsuleCylCm);
	return CapsuleMesh.Get();
}

// -- body info (formation shared fragment read) ------------------------------

bool UDemoUnitVisualSubsystem::GetUnitBodyInfo(FUnitHandle Handle, FDemoUnitBodyInfo& OutInfo) const
{
	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* Mass = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !Mass)
	{
		return false;
	}
	const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
	if (!Roster || Roster->Num() == 0)
	{
		return false;
	}
	const FUnitFormationSharedFragment* F =
		Mass->GetMutableEntityManager().GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]);
	if (!F || F->NumFiles <= 0 || F->NumSlots <= 0)
	{
		return false;
	}

	OutInfo = FDemoUnitBodyInfo();
	OutInfo.Handle = Handle;
	OutInfo.Team = TeamByUnit.FindRef(Handle);
	OutInfo.FacingYawDeg = F->FacingYaw;
	OutInfo.NumFiles = F->NumFiles;
	OutInfo.NumRanks = FMath::Max(1, FMath::CeilToInt(static_cast<float>(F->NumSlots) / F->NumFiles));
	OutInfo.FileSpacingM = F->FileSpacing * 0.01f;
	OutInfo.RankSpacingM = F->RankSpacing * 0.01f;
	OutInfo.SoldierCount = Roster->Num();
	OutInfo.bGarrisoned = (F->GarrisonMode != EGarrisonMode::None);

	// Body center = anchor (front-rank center) minus front * half rank depth
	// (identical to the soak controller's probe math).
	const float Rad = FMath::DegreesToRadians(F->FacingYaw);
	const FVector2D Front(FMath::Cos(Rad), FMath::Sin(Rad));
	const float HalfRanksM = static_cast<float>(OutInfo.NumRanks - 1) * OutInfo.RankSpacingM * 0.5f;
	OutInfo.BodyCenterCm = F->AnchorPos - Front * HalfRanksM * 100.f;

	if (const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(Handle))
	{
		OutInfo.UnitName = Spec->UnitName;
		OutInfo.InitialRoster = Spec->SoldierCount + Spec->OfficerCount;
		OutInfo.bGeneral = Spec->bIsGeneral;
	}
	// The visual blob center (the banners/circles track this); falls back to
	// the sim's formation center until the first visual tick fills the cache.
	OutInfo.VisualCentroidCm = CentroidByUnit.FindRef(Handle).IsNearlyZero()
		? OutInfo.BodyCenterCm
		: CentroidByUnit.FindRef(Handle);
	// PR-2 banner fields: rout state (the chip's color index), the live
	// morale/fatigue snapshot values, the ammo fraction (negative = not
	// missile-capable) and the fire-toggle read-back (the badges).
	if (const FUnitRuntimeState* State = Registry->FindUnitState(Handle))
	{
		OutInfo.RoutState = static_cast<uint8>(State->RoutState);
		OutInfo.bGuardMode = State->bGuardMode;
	}
	if (const FBattleAttributeSnapshot* Attr = Registry->FindCachedAttributes(Handle))
	{
		OutInfo.MoraleValue = Attr->Morale;
		OutInfo.FatigueValue = Attr->Fatigue;
	}
	OutInfo.AmmoFraction = Registry->GetUnitAmmoFraction(Handle);
	Registry->GetUnitFireToggles(Handle, OutInfo.bFireAtWill, OutInfo.bSkirmish, OutInfo.bOutOfAmmo);
	return true;
}
