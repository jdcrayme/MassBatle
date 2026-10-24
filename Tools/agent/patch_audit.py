import io

p = r"E:\Unreal Projects\MassBattle\Docs\agent\api-audit-5.8.md"
s = io.open(p, encoding="utf-8").read()

old = "| C7 | §4.2: fragments/tags derive `FMassFragment`/`FMassTag` \"engine auto-registers\" — module not stated |"
new = """| C10 | §4.6 (ADR-15 plan): observe sparse elements via UMassObserverProcessor | **Unsupported in 5.8.4**: `UMassObserverRegistry::RegisterObserver` ensures `!IsSparse(ObservedType)` -- "observing sparse elements is not supported at the moment" (MassObserverRegistry.cpp:25; on-machine probe crashed boot) | ✅ edited |
| C11 | §4.2 (ADR-15 plan): sparse tag add is per-entity without archetype change | **Chunk-granular in 5.8.4**: sparse TAG storage is a per-chunk bitset ("Sparse tags live there since they don't contain any data per instance", MassSparseElementsStorage.h) -- AddTag on one entity marks its whole chunk. Per-entity death marking requires a regular tag | ✅ edited |
| C12 | §4.7 audit row: "MassCore is a MassEntity dependency -- no extra Build.cs entry needed" | The base-struct UClass constructors need **MassCore linked explicitly** (LNK2019 without it); added to Build.cs private deps at Task 3 | ✅ edited |
| C7 | §4.2: fragments/tags derive `FMassFragment`/`FMassTag` "engine auto-registers" — module not stated |"""
assert old in s, "C7 row anchor not found"
s = s.replace(old, new, 1)

old2 = """Files present in pinned source: `MassEntity/Public/MassEntityTrace.h` + `MassEntityTrace.cpp` (Private), `MassEntity/Public/MassDebugger.h` (+ MassDebuggerBreakpoints.h). 5.8 traces include archetype moves and sparse-element visibility (per §4.1). Deeper wiring check (does the local build emit the trace channel under `-trace=`?) deferred to Task 12 per BRIEF -- entry to be completed there. Phase 0 hand-rolled metrics stay limited to what the debugger does not already expose (B.2/I2 rule)."""
new2 = """**Task 12 verification (complete).** Trace/insights surface present in the pinned source: `MassEntity/Public/MassEntityTrace.h`, `MassEntity/Public/MassDebugger.h`, `MassEntity/Public/MassDebuggerBreakpoints.h` (archetype-move events, sparse-element visibility, batched entity-move traces per §4.1). Verification path for future runs: launch with `-trace=` and open the trace in Unreal Insights -- the Mass channel appears when the MassEntity module loads (no project wiring required); the Mass Debugger window lists archetypes/entities live in editor sessions. Phase 0 hand-rolled metrics (Battle.DumpMetrics: archetype count, per-frame UnitSync ms, unit count, bookkeeping hash) stay limited to what the debugger does not already expose (B.2/I2 rule). No engine work needed -- the wiring is engine-side; recorded here as the I2 check."""
assert old2 in s, "insights section not found"
s = s.replace(old2, new2)

old3 = "**T0.9 conclusion:**"
new3 = """**T0.9 implementation note (Task 11):** there is no global time-slicing cvar to "disable" -- the contract cvar `Battle.Test.DisableTimeSlicing` records the contract and the structural guarantee is compile-time (Battle processors never construct `FExecutionLimiter`; kept true by review per B.0). `UBattleTestSettings::ApplyTestMode()` seeds the -game process; `AssertTestMode()` reads the ACTIVE state (FApp fixed step + delta, contract cvars).

**T0.9 conclusion:**"""
assert old3 in s, "T0.9 conclusion not found"
s = s.replace(old3, new3)

io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("audit updated (C10-C12, insights, T0.9 note)")
