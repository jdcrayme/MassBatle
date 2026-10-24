import io

p = r"E:\Unreal Projects\MassBattle\Docs\tw-battle-system-roadmap-and-testing.md"
s = io.open(p, encoding="utf-8").read()

old2 = """**Exit criteria:** 2 units × 40 entities spawned from table data; a GAS attribute change is visible in `FUnitStatsSharedFragment` within 1 frame; killing an entity flips archetype, fires signal, updates roster."""
new2 = """**Exit criteria:** 2 units × 40 entities spawned from table data; a GAS attribute change is visible in `FUnitStatsSharedFragment` within 1 frame; killing an entity flips archetype, fires signal, updates roster. *(Phase 0 probe note: the archetype flip stays -- ADR-15 evaluated 5.8 sparse tags and reverted: chunk-granular storage + observer incompatibility, see `Docs/agent/api-audit-5.8.md`.)*"""
assert old2 in s, "exit criteria not found"
s = s.replace(old2, new2)

old3 = """| T0.5 | Functional | Death path | `FDeadTag` removes entity from live queries within the stated flush contract -- buffer state asserted directly post-Death processor, or end-of-phase semantics [E5]; signal updates roster; queued destroy completes |"""
new3 = """| T0.5 | Functional | Death path | `FDeadTag` removes entity from live queries within the stated flush contract -- observer fires during the kill flush [E5]; signal updates roster; queued destroy completes. *(Implemented as Battle.Func.S0_DeathPath; ADR-15 final state: regular tag.)* |"""
assert old3 in s, "T0.5 row not found"
s = s.replace(old3, new3)

old4 = """**Test-mode contract** (all tests run under it; one switch, grouped as `Battle.Test.*` CVars):"""
new4 = """**Test-mode contract** (all tests run under it; one switch, grouped as `Battle.Test.*` CVars -- implemented by `UBattleTestSettings::ApplyTestMode()` and asserted against active engine state by `UBattleTestSettings::AssertTestMode()` in `Plugins/MassBattle/Source/MassBattle/Public/BattleTestSettings.h`; applied + asserted at every S-scenario start):"""
assert old4 in s, "test-mode contract line not found"
s = s.replace(old4, new4)

io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("roadmap patched (exit criteria, T0.5, B.0)")
