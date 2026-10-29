# Phase 3 Audit -- Ranged Stack (cumulative)

| | |
|---|---|
| **Date** | 2026-10-30 |
| **Auditor** | Senior-dev audit pass (independent of the implementation run) |
| **Subject** | HEAD `86d1a1f` -- the phase-3 ranged-stack run (Tasks 0-8, commits `3ee518e..6cffbe6`), the PR-sweep landing (`45faecd..44a1f9d`), and the ranged-facing surfaces they touched in the demo module and the test tier |
| **Method** | Full read of the ranged pipeline (both seams, both processors, the pool/subsystem, registry + save + state-hash ranged terms, ExecBridge, debug overlay, soak variant, the scenario actors) against the spec's RC-table. Static review only: no engine build in this environment; compile-sanity by symbol/include check, value-neutrality argued per change (identical bits out). |
| **Deliverables** | (a) this findings register; (b) six commits on branch `audit/phase3-redo` + `.patch` files `Patches/0017..0022`; (c) PR writeups `Patches/PRs/PR-10` + `PR-11` for the editor/machine-gated remainder. |

---

## 0. Verdict

The architecture claim held exactly as written: ranged really is a fire-control pass over the
existing spine plus a pooled SoA store, and the arrow really lands through the melee machinery
(one quantizer, one death path, zero new GAS attributes, zero new shared fragments). The
canonical-identity work (A13/RC8) that landed from the determinism blocker is the best code in
the phase -- five real defects found and fixed by one honest test.

The rot is the same genus as phase 2's, one generation later: **every constant the phase-2 audit
unified got re-scattered by the new code that copied the pattern instead of consuming the home**
-- the 40 cm body radius (phase 2's B-1) re-typed in three new ranged files plus one raw literal,
the Knuth hash multipliers (phase 2's B-2) re-typed in two new homes, the EDU row-index fixture
anti-pattern (phase 2's B-3) re-introduced in EVERY new fixture surface, and -- the worst of them --
the demo's faction split keyed on `EduRowIndex == 0`. Three of these carried comments claiming the
drift was the convention ("one named constant per seam"; "a future audit may unify"). It was not;
this audit is that future audit, and the unifications are now in.

Nothing here required redesign. Every fix is a rename to one home, a dictionary key, or a
deletion. The test tier needed NO removals this time -- the C-6 lesson (no permanently-failing
stubs) was respected, and the ranged suites are disjoint; the redundancy found was name-list
duplication in the scenario actors, not coverage overlap.

---

## 1. Findings register

Severity: **A** = correctness (fix now) · **B** = maintainability/data-drift (fix now, cheap) ·
**C** = cleanliness (fix now, trivial) · **D** = deferred (needs owner/editor/machine; PR filed).

### A-tier (correctness)

| ID | Sev | File(s) | Finding |
|---|---|---|---|
| **A-1** | Med | `DemoUnitActor.h` (`Team`), `AUnitActor.h:110` (`TeamId`), `DemoUnitVisualSubsystem.cpp:93-100` | **Two team notions on one authoring surface, uncoupled -- and the adopt path keyed on the wrong one.** The demo actor exposes `EDemoTeam Team` (visual faction, drives selection/tinting) AND the plugin actor's `uint8 TeamId` (sim side, drives the ranged friendly rules + skirmish threats). Nothing connected them: a level author who sets `Team=Enemy` but leaves `TeamId` at its default 0 gets an enemy-TINTED unit that the ranged rules treat as a FRIEND (holds fire to protect it, never retreats from it) -- silent, wrong, and invisible to every test (the demo map's placed units are a binary .umap). Worse, `AdoptUnknownRegistryUnits` derived the demo faction from `Spec->EduRowIndex == 0` -- EDU row ORDER as a faction notion, the exact anti-pattern phase 2's B-3 killed in the fixtures: re-order the golden EDU and the demo's faction split silently inverts. Fix (patched): OnConstruction syncs `TeamId = (uint8)Team` (ONE notion on the authoring surface); the adopt path reads `Spec->TeamId` (the side, not the row). The demo map's placed units must still be eyeballed in PIE -- PR-11 carries it (a .umap cannot be audited statically). |

### B-tier (magic numbers / data-in-code / duplication)

| ID | Sev | File(s) | Finding |
|---|---|---|---|
| **B-1** | High | `BattleRanged.cpp:162` vs `BattleEngagement.h:95` vs `ProjectileStepProcessor.cpp:72-73` vs `RangedFireProcessor.cpp:329` | **The 40 cm body radius re-scattered, with a comment claiming that was the rule.** Phase 2's audit fixed this as ONE home (`FCombatSolver::Contact::BodyRadiusM`, "keep it HERE -- a per-file retyped 0.4f drifts silently"); phase 3 then wrote three MORE homes: `VolleyBodyRadiusM` in the ranged seam (its comment: "B-1 family: one named constant per seam" -- a misreading that legitimizes the drift), file-local `BodyRadiusM`/`BodyTopM` in the step processor, and a raw `2.f * 0.4f` in the fire pass's blocker query. All copies agreed TODAY (0.4 everywhere) -- the failure mode is the silent desync on the next edit. Fix (patched): ONE home `BattleBody` in BattleTypes.h (the file every seam includes): `RadiusM 0.4 / EyeHeightM 1.6 / TopM 1.8 / AimCenterM 1.0`; `Contact::BodyRadiusM` becomes an alias; the eye height (was a re-typed `1.6f` in the header default AND the fire pass) and the aim-center height (was `+ 1.0f`) join it. Values identical everywhere; rename, not re-derivation. |
| **B-2** | High | `BattleBallistics.cpp:9-15` vs `BattleMelee.cpp:64-85` vs `BattleTypes.h:39-46` | **The hash-family multipliers in THREE homes -- two of which asked for this fix in their own comments.** Phase 2's B-2 moved the roll hash INTO FMeleeMath ("one home for the Knuth multipliers"); phase 3 then grew two more homes: BattleBallistics.cpp's file-local `KnuthGold`/`KnuthMix` ("a future audit may unify the constant's home") and BattleCanonicalEntityKey's inline literals. The two [0,1) folds even differ subtly (`/16777215.f` vs `/0x01000000u`) -- not a bug today (different functions), but a copy-paste trap. Fix (patched): ONE home `BattleHashFamily` in BattleTypes.h (`GoldenRatio 2654435761u, MixA 40503u, MixB 2685821657u`); all three sites consume it. Expressions unchanged; identical bits out; T2.1 + T3.1 goldens lock the same values they always did. |
| **B-3** | High | `BattleExecCommands.cpp` (SpawnVolley/RangedPerfStress/CombatPerfStress), `BattleMovementSoakController.cpp:164`, `DemoGameMode.cpp:168-179` | **The EDU row-index anti-pattern re-introduced in every new fixture surface.** Phase 2's B-3 fixed SpawnClash to dictionary-keyed lookups ("row indices are the worst part: anyone re-ordering the EDU silently turns the knights-vs-peasants mob into peasants-vs-peasants"); phase 3 then wrote `SpawnAt(2, ...)` / `SpawnLine(2, ...)` / `Min(2, Rows.Num()-1)` (the archer row as a raw index) in both ranged fixtures, the soak's volley mix, and CombatPerfStress (`SpawnLine(0, ...)`, pre-existing). The T6.1 constants audit WILL touch the golden EDU -- with an index-held fixture, re-ordering it silently turns the 1,000-archer perf gate into 1,000 crossbows (different flight times) or 1,000 levy (no fire at all). Fix (patched): `BattleExec::FindEduRowIndexByDictionary` promoted to the public API; every fixture resolves by dictionary name (`Peasant_Archers`, `Peasant_Levy`); the soak and the demo's third re-typed matcher (the FindRow lambda) consume the same helper. |
| **B-4** | Med | `BattleExecCommands.cpp` (2x), vs `BattleExecCommands.cpp:48` | **The ranged session-on flip list re-typed in both ranged fixtures** -- the exact duplication class phase 2's D-3 killed with `EnableDemoCombatPipeline()`. Same file also re-types the fixture path + load + parse block six times. Fix (patched): `BattleExec::EnableRangedPipeline()` + `LoadGoldenEduRows()`; SpawnClash's own block consumes the loader too. |
| **B-6** | Med | `ProjectileStepProcessor.cpp:245-248` vs `AssignPairsProcessor.cpp:440` | **The ContactAngle deg<->byte converters inline in processors.** The step pass's deg->byte quantization is a five-line expression in the impact hot path; the assign pass holds the inverse scale (`* 360.f / 255.f`). Both live beside the sectorizer that consumes them. Fix (patched): `FCombatSolver::ContactAngleFromDegrees` / `DegreesFromContactAngle`; formulas verbatim. |
| **B-7** | Low | `BattleRanged.h:48-56` vs `BattleRanged.cpp:114-141` | **The profile table has two homes** (the struct's member defaults describe the Arrow row; the switch re-states it). Runtime consumers only call `GetProjectileProfile`, so the defaults are near-dead -- but a future edit to one silently disagrees with the other. Not patched (the switch fully overrides both real kinds; collapsing the Arrow case onto the defaults buys one line and costs explicitness). Recorded so T6.1's constants swap touches BOTH sites deliberately. |
| **B-8** | Low | `RangedFireProcessor.cpp:497/520` vs `BattleRanged.cpp` (ComputeAmmoFraction) | **The fraction denominator re-derived twice in the fire pass** (overlay bar + GAS mirror), each with its own `Max(1, ...)` guard, while the seam's `ComputeAmmoFraction` is the authority. Fix (patched): both sites call the seam. |

### C-tier (cleanliness)

| ID | Sev | File(s) | Finding |
|---|---|---|---|
| **C-1** | Med | `RangedFireProcessor.cpp:85-104` | **The A14 GAS mirror scanned all actors PER UNIT PER TICK** (`TActorIterator` inside the per-unit loop -- O(units x actors) on the game thread every firing tick). Fix (patched): ONE sweep per pass into a `TMap<FUnitHandle, TWeakObjectPtr<AUnitActor>>` scratch (EC9), reset per pass; the mirror reads the map. |
| **C-3** | Low | `BattleFuncTestActor.cpp` (S6/Skirmish/DeterminismTwoRun Set+RestoreCvars) | **The ranged session-on cvar list re-typed as local arrays in three scenario classes** (6 copies of the 4-name list). Fix (patched): `RangedSetCvars`/`RangedRestoreCvars` beside the existing `ClashSetCvars` helper; three classes consume. |
| **C-6** | Med | `BattleRanged.cpp:184-186` vs `RangedFireProcessor.cpp:327-329` | **The arced impact-zone formula derived twice** (the seam's hold-fire zone count; the fire pass's blocker query -- same cone math, re-typed). Fix (patched): `ImpactZoneRadiusForDist` in the seam; the query reads it plus a named pad (`BlockerQueryPadM`). |
| **C-7** | Low | `RangedFireProcessor.cpp:437` (`+ 2.f`), `BattleRanged.cpp:222/226` (`0.3f` x2), `RangedFireProcessor.cpp:328-329` (`+ 2.f` x2), `BattleProjectileSubsystem.cpp:9` (`Max(16, ...)`) | **Unnamed mechanism margins** -- the TTL safety net, the LOS clearance (typed twice in the seam), the blocker-query pad (typed twice), the pool reserve floor. All patched to named constants with doc-comments (`FRangedConstants::TtlSafetyMarginS`, `FBallisticsConstants::LosClearanceM`, `BlockerQueryPadM`, `MinPoolReserve`). |
| **C-4** | Low | `BattleSpawnLibrary.cpp:177` | `StatsPayload.ArmorAdd = Row.Armor * 0.0f;` -- an expression that is always 0.0 written as a multiplication. Pre-phase-3 (phase 2's landing). Not patched (cosmetic; the comment beside it explains the intent). Recorded for the next spawn-path touch. |
| **C-5** | Carried | `BattleSpawnLibrary.cpp` (fixup) | **The spawn payload never stamps `FBaseStatsFragment.{Lethality, Shield}`** -- disclosed by the phase-3 run itself (task 5.6, "T6.1 owns completion"). Carried unchanged; T6.1's constants audit is the owning task. |

### D-tier (deferred -- editor/machine/owner; PRs filed)

| ID | Sev | Target | Finding |
|---|---|---|---|
| **D-1** | Med | `Content/MassBattle/Data/Units/` | **`BuildT0Configs` authors four config assets; only two are in the repo.** The command's table was extended for rows 2/3 (Peasant Archers, Mercenary Crossbowmen) but `M_Battle_PeasantArchers` / `M_Battle_MercenaryCrossbowmen` .uassets were never committed -- the code and the asset tree disagree, and the next `BuildT0Configs` run would silently re-author them with drift. PR-10. |
| **D-2** | Med | `ADemoUnitActor` authoring surface + `L_MassBattle_Demo01` | **Level-authored units reference EDU rows by INDEX** (`EduRowIndex` int property) -- the level-authoring equivalent of B-3, plus A-1's remainder: the demo map's placed units need a PIE eyeball to confirm their `TeamId`s match their factions (a .umap cannot be audited statically). PR-11. |
| **D-3** | Carried | `Tools/CI/det_*.log`, the staged nightly | **The two open blockers stay open**: the mixed two-run mm-scale divergence (next step: hex-float tracing of the servo's DesiredVel accumulation at ticks 11-14; `det_trace8.log` has the shape) and the pre-existing staged movement-soak dress-gate failure (phase-2 dossier territory). Both already scheduled by the phase-3 report §4-5; not re-litigated here. |
| **D-4** | Carried | `RangedFireProcessor.cpp` (mirror) | **The A14 GAS mirror's interim writer** (the fire pass, gated on the Debug pointer) -- disclosed by the run; UnitSync owns the sync path at Phase 4+. The per-pass cost fix landed (C-1); the HOME move is Phase 4's. No PR. |
| **D-5** | Low | `MassBattleTest` (8 files) | **The golden-EDU fixture path re-typed across the test module** (8 local `LoadFixture`-ish blocks). The plugin-side dedup landed (`LoadGoldenEduRows`); the test-side churn was deliberately left -- test-local fixture loading is a legitimate pattern, and touching 8 test files without a machine run is risk without payoff. Follow-up for whoever next edits those files. |

### Explicitly checked and found healthy (no action)

- **Flags-off identity**: both ranged gates read the cvars top-of-pass and return before any
  state; the step pass's empty-pool early-out and the fire pass's no-ranged-units skip are real
  (R-R8 verified at every call site).
- **The canonical-identity work (A13/RC8)**: `BattleCanonicalEntityKey/Count` feeding every
  hash-family draw, the sorted-order pass maps, the pool's index-order iteration -- the five
  defect fixes from the determinism blocker are genuine hardening, not test-chasing. The residual
  mm-scale transient is movement-side and honestly disclosed.
- **The killed-ledger's lifecycle** (`GKilledLedger`): considered for a "clear per pass" fix and
  REJECTED -- the deferred FDeadTag is flush-invisible while the last-tick hash still addresses
  the corpse, so cross-tick persistence within a volley is load-bearing; it clears at pool-empty
  (bounded by kills-per-battle; new-battle handles get new serials, so stale entries never
  alias). Left as-is deliberately.
- **State-hash term layout**: the ranged block's position-stable zero-fill and the pool block's
  index order are exactly per spec §3.4; melee-only hashes unchanged by construction.
- **The pool's fail-closed contract**: Acquire's drop path, Release's generation bump, the
  battle-start-only reserve -- all as ADR-27 wrote them.
- **Redundant-test scan**: the nine `Battle.Unit.Ranged.*` suites are disjoint (Ballistics vs LOS
  vs Pool vs AmmoAccounting vs VolleySelection vs SkirmishMath vs PoolStep vs FirePass vs
  Impacts); no superseded stub shipped this phase (the phase-2 C-6 lesson held); the scenario
  tier's asserts are sampled with evidence lines, not restatements.
- **A12 discipline**: `ServoProcessor`/`UnitLocomotion` gained zero ranged consumption -- verified
  by grep, not by the spec's word.

---

## 2. Fixes applied in this audit (branch `audit/phase3-redo`)

Each fix group is one commit; `.patch` exports under `Patches/0017..0022`. Every commit message
carries its MUST-re-run note (AGENTS §2).

| # | Patch | Groups | Risk | What it touches |
|---|-------|--------|------|-----------------|
| 17 | `one home for the soldier body geometry` | B-1 | None (same values, one source) | BattleTypes.h (+`BattleBody`), BattleEngagement.h (alias), BattleRanged.h/.cpp, RangedFireProcessor.cpp, ProjectileStepProcessor.cpp (+named `FlyoverQueryMarginM`) |
| 18 | `one home for the hash family multipliers` | B-2 | None (identical bits out) | BattleTypes.h (+`BattleHashFamily`), BattleMelee.cpp, BattleBallistics.h/.cpp |
| 19 | `one home for the ContactAngle conversions` | B-6 | None (formulas verbatim) | BattleEngagement.h/.cpp, AssignPairsProcessor.cpp, ProjectileStepProcessor.cpp |
| 20 | `fixture honesty + exec/demo dedup` | B-3, B-4, A-1 (code half) | Behavior-visible only in demo tinting (adopt reads TeamId) | BattleExecCommands.h/.cpp, BattleMovementSoakController.cpp, DemoGameMode.cpp, DemoUnitVisualSubsystem.cpp, DemoUnitActor.cpp |
| 21 | `fire-pass simplifications + named margins` | C-1, C-6, C-7, B-8 | Per-pass cost only (same writes, fewer scans) | BattleRanged.h/.cpp (+`FRangedConstants`, `ImpactZoneRadiusForDist`), BattleBallistics.h (+`LosClearanceM`), RangedFireProcessor.cpp, BattleProjectileSubsystem.cpp |
| 22 | `ranged session-on cvar list, one home (tests)` | C-3 | None (same sets flipped) | BattleFuncTestActor.cpp |

Commits are ordered so any prefix compiles; the whole set is value-neutral except patch 20's
demo-tinting change (units adopted by the visuals now tint by their SIDE, which is the honest
reading) and patch 21's per-pass amortization (same mirror writes, one sweep).

## 3. What needs editor/machine access (PR writeups, `Patches/PRs/`)

| PR | Title | Why it can't land from here |
|----|-------|----------------------------|
| **PR-10** | Author + commit the ranged config assets (D-1) | Needs the editor (BuildT0Configs run) + a binary .uasset commit |
| **PR-11** | Dictionary-keyed level authoring + the demo-map TeamId eyeball (D-2, A-1 remainder) | Needs the actor property change verified in PIE + the map re-saved; a .umap cannot be audited or edited statically |

Both carry motivation, exact files, the change sketch, the evidence to gather, and the roll-back
story, per the series format.

## 4. Evidence conventions

- All file:line references are against HEAD `86d1a1f` (pre-audit); post-audit references are to
  the `audit/phase3-redo` commits.
- "Identical values/bits" claims are structural: the substituted expressions read the same
  numbers from the same inputs through the same operations (renames of constants, aliases, and
  verbatim-formula moves). The golden tier (T2.1, T3.1-T3.3) must still be re-run on the machine
  per each patch's commit note -- no behavior claim above rests on static review alone.
- **Session lesson recorded (the kickoff's F-K class, again): the file-editing surface in this
  environment silently expands tabs to spaces on write.** It bit this audit's first three
  commits (whole-file reindent diffs; caught by numstat review, redone byte-exact via scripted
  splicing that fails loudly on anchor mismatch). Every patch in `0017..0022` was verified to
  apply cleanly, in order, on a pristine `main` tree, with brace balance and no leftover
  references to the removed constants.
