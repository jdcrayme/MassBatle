# Phase Garrison -- Progress Log

| | |
|---|---|
| **Date** | 2026-10-25 |
| **Engine** | UE 5.8.4 (ADR-13), E:\Unreal\UE_5.8 |
| **Project** | E:\Unreal Projects\MassBattle |
| **Protocol** | Docs/AGENTS.md standing protocol; brief §3.1 owner decisions pre-approved |
| **Run mode** | Single autonomous run; evidence via Tools/CI/run_tests.bat {unit,func} |

## Session log

- [task0.M] Kickoff: materialized companion docs into the repo (spec `Docs/spec/garrison-movement.md`, brief `Docs/phases/phase-garrison/brief.md` -- both existed only as owner-pasted attachments; added to the Docs/README.md map per AGENTS §7).
- [task0.M] Read order executed: README status block -> AGENTS.md -> brief -> spec §0/§2/§3/§4/§7 -> source spine (BattleMovementFragments.h, UnitLocomotionProcessor.cpp, FormationSlotResolveProcessor.cpp, BattleSlotAssigner.h, BattlePassGeometry.h, BattleCostGrid.h, BattlePathPlanner.h, BattleTypes.h, BattleRegistry.h, BattleStateHash.cpp) -> roadmap B.0/B.8 -> phase-1 harness/soak/scenario patterns. Facts F1-F16 matched the working tree; no discrepancies found at read time.
- [task0.M] Gate skeleton: `BattleGarrisonTypes.h` (enums + constants + record structs), `BattleGarrison.h/.cpp` stub seam + the six `Battle.Garrison.*` CVARs, TG.1-TG.5 unit stubs + TG.6-TG.9 func stubs (S8/S9/S10/S8b). Evidence: unit tier 45/50, the 5 failures exactly the garrison skeletons (last_run.json). Commit `3ad00f5`.
- [task1.M] Data spine: `UBattleGarrisonSubsystem` (RegisterStructure/FindStructure, TryBook first-fit over entrance slot blocks, entry leases, queue-wait slots, ReleaseUnit, DumpStructures); fragment fields on FUnitFormationSharedFragment (+ FGarrisonSlotFragment mirror, FBattlePathPoint.Z, FSlotTargetFragment.TargetZ); FUnitRuntimeState.PendingAfterEgress; state-hash garrison terms appended after formation terms; `Battle.Garrison.DumpStructures` probe. TG.4 body landed with this task (gate-first "prior one" allowance). One test-fixture bug fixed (ledger record had 20 slots/entrance, needed 40 for two 20-slot ranges). Evidence: unit tier 46/50 (4 fails = task-2/3 skeletons). Commit `932ac4b`.
- [task2+3.M] Cost stamping (`FBattleCostGrid::StampCost`), gray-box fixtures (`FGarrisonLayout::MakeFixture`: building 48x20 m 2 units/80 posts; wall 64 m walkway Z 6 m 48 posts; trench channel Z -0.75 m 48 posts; ClearWidthM 4 per R-G2), `StampStructure` (footprint Impassable, corridor boxes GarrisonCorridorCost), seam math (BuildInternalPath incl. egress direction, AssignSlots bounded greedy bijection, QueuePoints, ZAtS/ZAtSFragment, ColumnFilesFor), ExecBridge fixture commands (`Battle.Garrison.SpawnBuilding/Trench/Wall`). Early-landed the task-4 order surface for compile coupling: `IssueGarrisonOrder` (A9 cavalry guard) / `IssueExitGarrisonOrder` / parking API (A7) / F14 booking release on death+unregister.
- [task2+3.M] DEVIATION (disclosed for the handoff report): tasks 2 and 3 landed as one commit -- both are pure seam math living in `BattleGarrison.cpp`, authored together; gate-first is preserved (every TG test landed with its implementation). TG.4's remaining order-surface coverage lands in task 4's own commit.
- [task2+3.M] Discrepancy found + fixed during verification: the cleaned `MakeFixture` rewrite dropped `OutRecord = MoveTemp(Rec)` -- the function returned true but left the caller's record untouched (surface symptom: cross-module-looking garbage; root cause a plain missing assignment). Diagnosed via a MaxUnits sentinel + AddInfo diagnostics in TG.2/TG.3; two test-side corrections along the way (trench Z monotonicity is DESCENDING by design; StampStructure restamp idempotence asserted on final grid state, not stamp count -- corridor boxes stamp last over the footprint by design). Evidence: unit tier 50/50 green, exit code 0.

