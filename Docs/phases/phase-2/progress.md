# MassBattle Phase 2 Progress Log -- Formation Movement Cleanup

| | |
|---|---|
| **Date** | 2026-10-27 |
| **Engine** | UE 5.8.4 at `E:\Unreal\UE_5.8` |
| **Project** | `E:\Unreal Projects\MassBattle` |
| **Protocol** | `Docs/AGENTS.md` + `Docs/phases/phase-2/brief.md` (owner-approved Formation Movement Cleanup Plan) |

## Log

- [kickoff] Brief transcribed from the senior-dev pass-back plan (ASCII/LF); repo re-grounded: the plan's audit HEAD `1200455` predates the TW fan-in peel (`63277a0`) and audit fixes (`41416e6`..`6eaacf9`). Current-HEAD deltas recorded in brief section 1: arrival facing now SNAPS (ArriveRefront) instead of slewing; a binary closer-walk peel exists; the blend / AssignFinal / feed-forward / member clamp / wheel-cap v2 / straggler pace / stickiness do not.
- [A1] PrevTargetPos + bPrevValid on FSlotTargetFragment; resolve writes prev pre-overwrite in both branches; FUnitServo::ComputeServoVelocityFF (V_slot + pursuit, clamped); ServoProcessor gated by Battle.Movement.FeedForward=0 -> old math byte-for-byte -> unit tier green (one test fixed: zero-DT case had a clamped expectation; 53/53 after).
- [A2] Servo ceiling = WalkSpeed x Stats->SpeedMult under Battle.Movement.MemberClamp (stats shared fragment added to ServoQuery); SpeedLimit<=0 fallback keeps walk ceiling.
- [A3] FSpeedLimitInputs gains HalfFrontageM + bPathActive; marching form r+HalfFrontage under Battle.Movement.WheelCapV2; pivot form preserved; header doc-comment updated; worked-example golden pinned (0.683 m/s).
- [A-gate] S1 slot-error instrumentation landed; ERRATA: the plan's 0.8 m wheel assert does not fit S1 (90-deg lateral reform of a 24 m unit; smoothed path never lets the cap bind -- flag-on anchor ticks identical to baseline: march 1223, wheel 966 both states; error 17.2 off / 16.9 on). Kept log-only; binding B-6 assert deferred to bend-course coverage. Evidence: Tools/CI/phase2_runs/s1_off.log s1_on.log. Second finding: -ExecCmds multi-command cvar chains execute only the FIRST command in -game -> added harness -Phase2Flags param + run_tests.bat "funcon" tier (FPhase2FlagScope applies/restores 6 cvars).
- [A-verify] unit tier 53/53; S1 flag-off Succeeded; S1 flag-on Succeeded (after assert removal).
