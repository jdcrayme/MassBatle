# T6.1 prep -- the morale threshold co-audit (the morale half of T6.1, pulled forward)

| | |
|---|---|
| **Date** | 2026-11-01 |
| **Branch** | `t61-morale-threshold-audit` (off `main` @ `b6b12ee`) -- PENDING OWNER SIGN-OFF; the flip commit lands after the owner approves (or amends) this proposal |
| **Scope** | the `Battle.Morale.*` threshold family + the `FMoraleDiscipline::BreakOffset` table, co-audited against the EDU fixture's `stat_mental` distribution (spec morale-and-gas section 4.4/4.12; ADR-37's swap-with-diff-review posture) |
| **Unblocks** | the `Battle.Morale.Enable` default flip (evidence-blocked since PR-12) + the demo beat 4/5 placeholder timings (spec demo-shell section 3 note) |
| **Not in scope** | the weights (`FMoraleWeights`), the fatigue bands, the fixture `stat_mental` values (all stay), the ranged/combat constant families (the other T6.1 halves, Phase 6) |

## 1. The problem (the PR-12 evidence block, quantified)

PR-12's flip probe measured `PairLifecycle` regressing with `MoraleRout Unit=1:1 State=Wavering Morale=2.00` at spawn and `State=Routing Morale=0.62` after casualties. The root cause was recorded as "two A16 placeholder families never co-audited". This audit performs that co-audit: the threshold family (Wavering 4.0 / Break 0.0 + discipline offsets / Rally 6.0) against the data the thresholds consume (the golden fixture's `stat_mental[0]` = 2, 4, 5, 6, 9).

The arithmetic of the placeholder family on the fixture data (grind-only, `CasualtyFraction` weight -8 per unit fraction; `SpikePerCasualty` 1.0 x weight -0.5 per fresh death):

| Row | Base | Disc | Placeholder Wavering 4.0 / Break 0.0 + (Low +2) | Verdict |
|---|---|---|---|---|
| Peasant_Levy (40) | 2 | Low | **wavers at spawn** (2 < 4.0); routes at **1 death** (2 - 0.2 = 1.8 < 2.0) | the PR-12 regression, exactly |
| Peasant_Archers (40) | 4 | Low | wavers at 1 death (4 is not < 4.0, but 3.8 is); routes at 11 | one-casualty wavering, thrash |
| Mercenary_Crossbowmen (40) | 5 | Normal | wavers at 6 dead; routes at 21 | in-band by luck |
| Sergeant_Spearmen (40) | 6 | Normal | wavers at 11 dead; routes at 26 | in-band by luck |
| Feudal_Knights (24) | 9 | Impetuous | wavers at 16 dead; routes at 19 | in-band |

The structural defect: the wavering threshold sat ABOVE half the roster's base morale, and the Low-discipline break offset (+2.0) was larger than the levy row's entire base (2.0) -- so the lowest-base row spawned inside the wavering band and routed at first blood. A threshold family must be placed relative to the data's minimum base, not above it.

## 2. The M2TW anchors (the T6.1 reference)

- **Break at zero, absolute.** The M2TW engine routes a unit when total morale (base + situational modifiers) drops below zero, with "wavering" the low band just above; the base `stat_mental` value itself carries the unit-to-unit spread. Our absolute-threshold design (spec section 4.4) already has this shape; the placeholder's 4.0 wavering placement did not.
- **The casualty curve.** The community-documented M2TW casualty penalty is -2 at 25% losses, -4 at 50%, -6 at 75% -- our `FMoraleWeights::CasualtyFraction` -8 per unit fraction reproduces that curve exactly (0.25 -> -2.0, 0.5 -> -4.0, 0.75 -> -6.0). The weights were already M2TW-shaped; only the threshold placement was off.
- **The base-morale distribution.** Peasants sit at 1-3 (`low`, `untrained`), line militia/spearmen 4-6, elite cavalry ~9 (Feudal Knights morale 9), elite ceiling ~11-13. The fixture rows (2, 4, 5, 6, 9) sit inside the community bands. Sources: the HeavenGames EDU guide (syntax + morale role), the imtw EDU tutorial (the value bands), the GameFAQs knights FAQ + the TWC wiki Feudal Knights page (morale 9), the TWC steam EDU thread (working examples). Links in the record's source register below.

## 3. The proposed family (the swap)

| Constant | Placeholder | Proposed | Grounding |
|---|---|---|---|
| `Battle.Morale.Enable` | 0 | **1** | the flip this audit unblocks; the PR-12 block was threshold placement, not the flag |
| `Battle.Morale.WaveringThreshold` | 4.0 | **1.0** | one point above the M2TW break-at-zero anchor, BELOW the fixture's lowest base (2) -- nothing wavers at spawn; a base-2 levy wavers after 6 grind deaths (F 0.15) or 3 fresh-spike deaths |
| `Battle.Morale.WaveringHysteresis` | 1.0 | 1.0 (kept) | recovery line = 2.0, above the levy base exactly; the R-M5 anti-thrash margin survives |
| `Battle.Morale.BreakThreshold` | 0.0 | 0.0 (kept) | already the M2TW anchor |
| `FMoraleDiscipline::BreakOffset` | {+2, 0, -2, 0} | **{+0.5, 0, -0.5, 0}** | the offsets must stay small against the base spread (2..9): the placeholder +2.0 equaled the levy row's whole base. +0.5 keeps the designed asymmetry (Low routes at < 0.5 -- after ~19% losses where a Normal unit of the same base needs ~25%); Disciplined -0.5 (no disciplined fixture row today; the table is future-proofed) |
| `Battle.Morale.RallyThreshold` | 6.0 | **1.5** | reachable by the LOWEST base under the general aura (levy 2 + aura 2.0 = 4.0; needs the general within ~27 m of a 20%-lost router) and unreachable without it (a routed levy sits below 0.5) -- the placeholder 6.0 made levy and peasant archers permanently unrallyable, breaking the D.12 story for low-tier units. 1.5 > Wavering 1.0, so a rally re-enters Wavering (morale 1.5 < hysteresis line 2.0) per spec section 4.5's "re-enter Wavering, self-issue Stop" |
| everything else (RallyBase 0.4, RallyDecay 0.5, MaxRallies 2, Spike/Officer/Shock/Cascade/Aura constants) | -- | unchanged | not implicated by the PR-12 evidence; the OfficerDeath 3.00 golden and the S7 cascade shape depend on them and stayed green |

### The audited family on the fixture data

| Row | Base | Disc | Waver (grind / fresh-spike) | Break (grind) | M2TW reading |
|---|---|---|---|---|---|
| Peasant_Levy | 2 | Low | 6 dead / 3 fresh | 8 dead | peasants grind out fast under fire |
| Peasant_Archers | 4 | Low | 16 / 7 fresh | 18 | militia archers collapse once losing |
| Mercenary_Crossbowmen | 5 | Normal | 21 / 9 fresh | 26 | paid missile troops, middle band |
| Sergeant_Spearmen | 6 | Normal | 26 / 11 fresh | 31 | line infantry holds to real attrition |
| Feudal_Knights | 9 | Impetuous | never (grind) | never | knights break only under stacks: flank -0.75/sector, general death -3.0 team-wide, cascade -1/router friend |

Rally reachability at 1.5: a routed levy (morale ~0.4 at 20% losses) needs the general's aura >= 1.1 -> within ~27 m of the 60 m radius; sergeants (routed at ~0) need <= 30 m. "Ride the general to the router" -- the D.12 beat -- is a real maneuver, not a formality.

## 4. Verification evidence (candidates in tree, defaults live, no session flags)

Branch `t61-morale-threshold-audit`; build MassBattleEditor green (108.8 s).

- **Unit tier: 84/84** (`Tools/CI/last_run/unit.log`, headless editor-cmd): `passed=84 failed=0 total=84 completed=True`. Includes the updated `Battle.Unit.Morale.Transitions` discipline cases (the offset-table scenarios re-valued: Normal breaks in the raw band at -0.25 where Disciplined holds; Low routes at 0.25 where Normal wavers) and `Battle.Smoke` running morale-on.
- **Func tier: 24/27 = exactly the three known** (`Tools/CI/last_run/func.log`, standalone -game): `passed=24 failed=3 total=27`; FAILED: `DeterminismTwoRun`, `S4_WidthOverride`, `S5_Mob` -- the pre-existing Tasks 1-2 set, byte-for-byte the main-baseline failures; NO new failures. The flip-critical probes:
  - `EXEC_RESULT PairLifecycle PASS handoff=1 disengage=1` -- the PR-12 regression is cleared by the constant swap (spawn-steady levy, no mid-fight rout at the probe's attrition).
  - `EXEC_RESULT S0_SyncLatency PASS` -- the leaked-Enable dirty-flag concern (BattleFuncTestActor.cpp note, line ~6943) did not materialize under default-on.
  - `S7_RoutCascade`: `wavering at tick 18 (kills=1)` < `routing at tick 50 (kills=4)` -- the order assertion holds; the cascade entries logged (`Unit=2:1 State=Wavering Morale=5.26`, `Unit=3:1` same tick) from the neighbor's rout.
  - `EXEC_RESULT OfficerDeath ... ratio=3.00 (x3 golden)` -- the spike-amplification golden survives.
  - The T4 scenarios' local `MoraleSetCvars` isolation held (they set and restore their own thresholds; the defaults swap did not touch their assertions).

## 5. Residuals and follow-ups (recorded, not blocking sign-off)

1. **The fixture's `impetuous` on Feudal_Knights** is a documented-deviation candidate: vanilla Feudal Knights read `disciplined` (or `low` in some rosters); `impetuous` is the M2TW flag for chivalric-order-type units. It serves the demo's charge showcase (C1/C2/E5 carry the row), and its mechanical effect here is the auto-charge envelope (MC9) + RallyFactor 0.8. Owner call; a row change rides the golden re-sign.
2. **The nightly re-baseline**: the flip moves the baseline soak (smoke + movement soak now run morale-on; SoakRout sets its own and is unaffected). First post-flip nightly re-baselines via the `write_baseline.py` flow.
3. **The demo beat 4/5 re-clock -- EXECUTED post-flip** (2026-11-01, `Tools/CI/m7_reclock_boot.log` + `m7_reclock_boot2.log`, the one-command `Demo.QuitAt` shape WITHOUT the probe args): the audited family produces the real cascade on the demo map -- `GeneralDeath Team=1 Shock=+6.00` (the beat-4 kill) and **3 organic enemy routs** (E1 levy at morale 0.17, E2/E3 sergeants at -1.18/-1.00, all crossing the audited break points from grind + shock + cascade; the PR-12-era stalemate where NO rout fired without the probe is gone), with beats 0-6 landing at t=0/8/18/30/85.1/105.1/120.1 (the clash-relative offsets hold). TWO findings for the owner: (a) **the verdict (beat 7) does not fire naturally** -- a 700-game-s boot shows the field goes STATIC after the enemy infantry line breaks (zero kills past t~300): the survivors (E4 crossbows, C2 knights) are never ordered forward and no auto-acquisition exists, so `Demo.ResolveProbe` STAYS as the D.17/D.18 arc-completion driver -- re-documented as ending the endgame standoff, not masking broken thresholds; (b) `Rallies=0` / beat-5 `Rally Router=none` -- the winning side does not route under the audited family, so beat 5 rode its field-center fallback; scripting a wavering friendly for the D.12 demonstration is an owner design call.
4. **The spec tables update rides the flip commit**: morale-and-gas section 4.12's default column, the demo-shell section 7 constraint line, and the `Battle.Func.S2_PassThrough` cavalry-proxy note stay as-is.
5. **The other T6.1 halves** (ranged/combat constant families, the friendly-fire port-or-deviation resolution) remain Phase 6; this record covers the morale-threshold half only.

## 6. Source register (M2TW references, tier B/C community documentation)

- HeavenGames, "A guide to the export_descr_units.txt file -- Medieval 2": https://medieval2.heavengames.com/m2tw/mod_portal/tutorials/export_descr_units_file_guide
- imtw EDU tutorial (stat_mental value bands): https://imtw.org/tutorials/article/368-tutor-po-export-descr-unittxt-v-medieval-2-total-war
- GameFAQs Knights FAQ (Feudal Knights morale 9): https://gamefaqs.gamespot.com/pc/254795-medieval-ii-total-war-definitive-edition/faqs/81512/knights
- TWC wiki, Feudal Knights (M2TW unit): https://wiki.twcenter.net/index.php?title=Feudal_Knights_(M2TW_Unit)
- TWC steam thread, "How does one edit unit stats?" (working stat_mental examples): https://steamcommunity.com/app/4700/discussions/0/611702631249477322/

Accessed 2026-11-01. The exact vanilla EDU lines are verifiable from the unpacked `data/export_descr_unit.txt` (the unpacker route); the audit's claims rest on the distribution bands, not per-row exactness.
