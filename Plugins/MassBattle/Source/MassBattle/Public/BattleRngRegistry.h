// BattleRngRegistry.h -- seeded RNG registry (roadmap B.5, T0.7, ADR-11 hooks).
// Pure struct + a thin world subsystem wrapper. One named FRandomStream per
// system; all streams derive from a per-battle root seed -- no global RNG, no
// unseeded defaults (a seed-dependent suite failure means a leaked global).
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "BattleRngRegistry.generated.h"

/** One stream's save state: name + current position seed (FRandomStream
 *  mutates its Seed in place, so the current seed IS the stream state;
 *  restoring constructs FRandomStream at the captured state). */
struct MASSBATTLE_API FBattleRngStreamState
{
	FName Name;
	int32 CurrentSeed = 0;
};

struct MASSBATTLE_API FBattleRngRegistry
{
	/** Derive a stream seed from the battle root: HashCombine(root, hash(name)). */
	static int32 DeriveSeed(int32 RootSeed, FName StreamName)
	{
		return HashCombine(RootSeed, GetTypeHash(StreamName));
	}

	void Seed(int32 InRootSeed)
	{
		RootSeed = InRootSeed;
		Streams.Reset();
	}

	FRandomStream& GetStream(FName StreamName)
	{
		FRandomStream* Existing = Streams.Find(StreamName);
		if (!Existing)
		{
			Existing = &Streams.Add(StreamName, FRandomStream(DeriveSeed(RootSeed, StreamName)));
		}
		return *Existing;
	}

	int32 GetRootSeed() const { return RootSeed; }
	int32 GetStreamCount() const { return Streams.Num(); }

	// -- save/load skeleton (A8, spec §3.5): stream states are snapshot in
	//    sorted-name order (deterministic) and restored at the captured state. --

	TArray<FBattleRngStreamState> SnapshotStreams() const
	{
		TArray<FName> Names;
		Streams.GetKeys(Names);
		// Lexical order: FName has no operator< for the default TLess; the
		// snapshot must be deterministic regardless of TMap layout.
		Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		TArray<FBattleRngStreamState> Out;
		for (const FName& Name : Names)
		{
			FBattleRngStreamState Entry;
			Entry.Name = Name;
			Entry.CurrentSeed = Streams.FindChecked(Name).GetCurrentSeed();
			Out.Add(Entry);
		}
		return Out;
	}

	void RestoreStreams(int32 InRootSeed, const TArray<FBattleRngStreamState>& States)
	{
		RootSeed = InRootSeed;
		Streams.Reset();
		for (const FBattleRngStreamState& Entry : States)
		{
			Streams.Add(Entry.Name, FRandomStream(Entry.CurrentSeed));
		}
	}

private:
	int32 RootSeed = 0;
	TMap<FName, FRandomStream> Streams;
};

/** Thin world-subsystem wrapper (game thread). Phase 1 systems pull their
 *  named stream from here; determinism claims are scoped per B.5/ADR-11. */
	UCLASS()
class MASSBATTLE_API UBattleRngSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Seed(int32 RootSeed) { Registry.Seed(RootSeed); }
	FRandomStream& GetStream(FName StreamName) { return Registry.GetStream(StreamName); }
	const FBattleRngRegistry& GetRegistry() const { return Registry; }
	FBattleRngRegistry& GetMutableRegistry() { return Registry; }

private:
	FBattleRngRegistry Registry;
};
