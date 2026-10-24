# Phase 1 -- Progress Log

Session: 2026-10-24 · Engine UE 5.8.4 (CL 58902052) at E:\Unreal\UE_5.8 · Project E:\Unreal Projects\MassBattle · Protocol: Docs/AGENTS.md + Docs/phases/phase-1/brief.md

## Discrepancies & deviations (dated)

### 2026-10-24 -- kickoff: working tree NOT clean (pre-existing uncommitted phase-0 work)
- `git status` at kickoff showed the phase-0 audit fix pass (source + CI files, 38 files, +591/-2603) and the 2026-10-25 Docs reorganization (Docs/{AGENTS,README}.md, phases/, reference/, spec/, reviews/; deletions of Docs/agent/* and flat-layout files) sitting **uncommitted** in the working tree. Docs/README.md + audit.md §7 record both as COMPLETE.
- Reality wins: the work exists and is coherent (MassBattleEditor build green on arrival; unit tier result logged below once finished). Committed as-found in two disclosed housekeeping commits (`phase0: ...` prefixed) before the first phase-1 commit, so per-task phase-1 commits stay clean.
- `Docs.zip` + `Docs/old/Docs-old.zip` are backup artifacts of the docs reorganization (owner's safety net); left on disk, added to `.gitignore`, not committed.

## Log

- [task0] Read order complete (brief §0 items 1-8: brief full; engine-api Part A §2-3 + Part B §4.3-4.5; arch §8/§4-note/§12/§3.1-3.3; roadmap Part A Phase 1 + T1 table + B.0/B.1/B.3/B.7/B.8; phase-0 report §7; audit §2 M1-M3 + §5; findings E2/R2/R4/R5; mcp-playbook §3/§8).
- [task0] git status check: tree dirty with pre-existing phase-0 work -- see discrepancy entry above; editor closed at kickoff (kernel stretch can run headless, brief §2 holds).
- [task0] Baseline validation of the as-found state: MassBattleEditor build `Result: Succeeded` (UBT, editor target, 4.5 s up-to-date); `run_tests.bat unit` -> `[summarize] tier=unit passed=16 failed=0 total=16 completed=True exit=0 ok=True`.
- [task0] Housekeeping commits: `12117e4` (docs reorganization, 20 Docs/ files), `fead81c` (phase-0 audit fix pass + CI wiring + .gitignore for Docs zips, 30 files), `b806d20` (phase1: kickoff). Tree clean after.
- [task1] §4.4 checklist audited against pinned source -- all rows verified; corrections C13-C20 recorded in `Docs/reference/engine-api-5.8.md` Part A §14 (movement-stack audit) + §4.6 sources merged into Part C. Headline facts: MassNavigation home is `Engine/Plugins/AI/MassAI/...` (C13, brief said Runtime/); SteerToMoveTarget goal force references previous DESIRED velocity not actual velocity (C14); ApplyMovement hard-requires FMassCodeDrivenMovementTag (C15); ApplyForce clamps MaxSpeed only (C16); engine avoidance hard-requires FMassMoveTargetFragment AND FMassNavigationEdgesFragment (C17); avoidance grid iteration is distance-sorted (C18); MassEntityHandle.h is a 5.8 deprecated shim (C19); FMassDesiredMovementFragment + Min(0.1,DT) clamp + force-zeroing recorded (C20). No cm-vs-m mixing in FMassMovementParameters (all ForceUnits cm-explicit).
- [task1] Velocity-fragment adoption decision (§4.4 item 6): adopt engine `FMassVelocityFragment` -- recorded as arch §3.1 note (authorized A7-style patch, `Docs/spec/architecture.md` §3.1).
- [task1] `MassBattle.Build.cs` += "MassMovement" private dep (fact 1.8; MassNavigation unlinked per A1). A4 include fix: `MassEntityHandle.h` -> `Mass/EntityHandle.h` in BattleTypes.h + BattleFuncTestActor.h (C19).
- [task1] Verify: UBT `Result: Succeeded` (53 s, new dep compiled); `run_tests.bat unit` -> passed=16 failed=0 ok=True. Commit `7fd196a`.
