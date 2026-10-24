import io

p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Public\BattleRegistry.h"
s = io.open(p, encoding="utf-8").read()
old = """	/** Death-path bookkeeping (arch §5.3): remove one entity from the unit's
	 *  roster. Called by the death-signal receiver on the game thread. */
	void HandleUnitDeath(FUnitHandle Unit, FMassEntityHandle Entity, int32 RosterIndex);"""
new = """	/** Death-path bookkeeping (arch §5.3): remove one entity from the unit's
	 *  roster. Called by the death-signal receiver on the game thread. */
	void HandleUnitDeath(FUnitHandle Unit, FMassEntityHandle Entity, int32 RosterIndex);

	/** DeathScan dedup: has this entity's death been signaled already?
	 *  (Sparse tag persists until destroy; without the handled set the scan
	 *  would re-signal every tick. Entries are bounded by deaths per battle.) */
	void MarkDeathHandled(FMassEntityHandle Entity);
	bool IsDeathHandled(FMassEntityHandle Entity) const;"""
assert old in s, "header anchor not found"
s = s.replace(old, new)

old3 = """	/** Per-slot generation counter; persists across free/reuse so stale
	 *  handles (old Serial) never alias a new tenant. */
	TMap<uint32, int32> NextGenerationByIndex;"""
new3 = """	/** Per-slot generation counter; persists across free/reuse so stale
	 *  handles (old Serial) never alias a new tenant. */
	TMap<uint32, int32> NextGenerationByIndex;

	/** Entities whose death has been signaled (DeathScan dedup). */
	TSet<FMassEntityHandle> HandledDeaths;"""
assert old3 in s, "member anchor not found"
s = s.replace(old3, new3)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)

p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\BattleRegistry.cpp"
s = io.open(p, encoding="utf-8").read()
old2 = """void UUnitRegistrySubsystem::HandleUnitDeath(FUnitHandle Unit, FMassEntityHandle Entity, int32 RosterIndex)"""
new2 = """void UUnitRegistrySubsystem::MarkDeathHandled(FMassEntityHandle Entity)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	HandledDeaths.Add(Entity);
}

bool UUnitRegistrySubsystem::IsDeathHandled(FMassEntityHandle Entity) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	return HandledDeaths.Contains(Entity);
}

void UUnitRegistrySubsystem::HandleUnitDeath(FUnitHandle Unit, FMassEntityHandle Entity, int32 RosterIndex)"""
assert old2 in s, "cpp anchor not found"
s = s.replace(old2, new2)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("registry death-handled set added")
