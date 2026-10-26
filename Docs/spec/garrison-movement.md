# Garrison Movement -- Design Specification (Buildings, Walls, Trenches)

**Project:** UE5 Mass Entity + GAS battle simulation (Medieval 2: Total War clone)
**Status:** Draft 1.0 · 2026-10-25 -- plan prepared for owner review; not yet executed
**Engine pin:** Unreal Engine 5.8.x (ADR-13)
**Audience:** Owner + implementing agent (companion to the phase brief)
**Companion:** `Docs/phases/phase-garrison/brief.md` (executable task plan) · `Docs/spec/architecture.md` (parent spec; §8 movement, §13 open decisions) · `Docs/spec/roadmap-and-testing.md` (gate conventions, B.8 DoD)

---

## 0. Reference behavior (Empire: Total War, to replicate)

The feature is defined by observable ETW behavior, not by a ported formula. The mechanics below are the acceptance contract for this phase; each maps to a mechanism in §3-§6.

| # | ETW behavior | MassBattle mechanism |
|---|--------------|----------------------|
| B1 | Player selects unit(s), clicks a garrisonable structure; the unit walks to it, files through the entrance in single file, and spreads to firing positions inside. Distribution is automatic -- the player gets no per-man control (a documented community frustration; it confirms auto-distribution is authentic). | `Garrison` order -> booking -> approach path -> internal entry path -> structure slot assignment (§5). |
| B2 | Structures have an occupancy capacity (in units; hovering shows how many fit). A full structure refuses further garrisons; late units wait outside. | Booking ledger with disjoint slot ranges + per-entrance entry lease + queue holds (§6). |
| B3 | Choke filing: soldiers enter one at a time through the door; the rest queue at the entrance. | Column morph (`FUnitPassGeometry` decision, `PassLocked` commit) along a structure-owned internal path (§4.3). |
| B4 | Walls: units climb stairs/ramps, spread along the wall walk, and stand at parapet positions facing outward. | Wall record = stairs+walkway internal path with Z, wall-top slot line with outward yaw (§7 elevation model). |
| B5 | Trenches / earthworks: units enter via access ramps and occupy sunken firing positions along the channel. | Trench record = ramp internal path, sunken slot line at negative Z (§7). |
| B6 | A move order out of the structure makes the unit file back out the way it came and re-form outside. | Egress phase = reverse internal path with column filing, re-form at the assembly point (§5.5). |
| B7 | Garrisoned units hold position at their posts (firing/melee from positions is later-phase territory). | Movement-only scope: `Settled` phase is a hold; servo states unchanged (§1 non-goals). |

Out of ETW scope for this phase (explicitly): assault-side combat (attacker entry fights, room clearing, evictions), building destruction effects on occupants, auto-deployment of defenders at battle start, artillery suppression of garrisons, capture-point logic. Combat hooks (cover flags, firing arcs on slots) are deliberately **not** built per owner decision -- §8 records the extension points so the engagement solver phase can consume them later without rework of the data model.

## 1. Scope

**In scope:** garrison orders for infantry-class units into three structure types (building, wall, trench); movement-only occupation; capacity booking with multi-unit support; egress; cost-grid integration; elevation (Z) at slot/path level; test gates TG.1-TG.10; debug overlay + metrics.

**Out of scope (binding):** combat integration of any kind (no cover metadata, no firing arcs, no engagement-solver changes); cavalry into buildings (v1 restricts to infantry -- cavalry structs EDU class is already tagged; a guard clause is enough); representation concerns (window visibility, pose/crouch animation -- Phase 5); ladders/siege towers (architecture Open Decision 13.5 remains deferred; this phase does not build the ladder movement domain); auto-garrison at battle start; editor-authored structure actors (gray-box C++/ExecBridge fixtures only; the registration API is editor-agnostic, so authoring tools land later without sim changes).

## 2. Architecture summary -- how garrison fits the existing spine

The core design claim, stated once and referenced everywhere below:

> **Garrison is a slot-source and path-source mode of the existing movement spine, not a new movement system.**

The Phase 1 stack is: `UnitLocomotion` (unit = locomotor: anchor, path, facing, speed limit) -> `FormationSlotResolve` (slot targets from path-frame/anchor layouts) -> `Servo` (velocity servo, per-state avoidance) -> `Integrate` (transform integration). None of the downstream stages care *where* slot targets come from. Garrison therefore reduces to four changes:

1. **Slot source branch.** Garrisoned units resolve slot targets from a structure slot table (copied into the shared fragment) instead of the field layout math. `FSlotAssignmentFragment::SlotIndex` keeps its role -- it indexes the unit's booked slot range instead of the formation grid.
2. **Path source branch.** Inside a structure the unit traverses a **structure-owned internal path** (queue -> door -> distribution node; stairs -> wall walk), fed into the existing `FBattlePathPolyline` / `PathS` anchor-progression machinery. Field A* remains responsible only for the approach to the entrance queue point.
3. **Z elevation.** Planar sim (X/Y) is preserved; Z becomes a slot/path-carried value with a small Z servo in Integrate. No layered nav domain is built.
4. **Booking + phase machine.** A game-thread booking ledger (capacity, slot ranges, entry lease) and a per-unit garrison phase FSM (`Approach -> Queue -> Ingress -> Occupy -> Settled -> Egress`) drive the branches above.

Consequences that drove this shape (each traced to a verified code fact):

- `FormationSlotResolve`, `Servo`, `Integrate` are **worker-safe and read fragments only** (`bRequiresGameThreadExecution = false`; fragment-only accesses). Structure slot data must therefore travel **inside the shared formation fragment**, copied game-thread by `UnitLocomotion` exactly like `PolylineFromPlan` copies planner paths today (fact F3 in the brief).
- No new tags, no new processors, **no archetype churn**: garrison state lives as fields on the existing `FUnitFormationSharedFragment`. This preserves the uniform movement spine (6 archetypes, T1.8 soak invariant) and follows the ADR-15 lesson (regular tags flip archetypes; ADR-15 evaluated sparse tags and reverted).
- The servo state table is untouched: garrisoned soldiers reuse `ManeuverToSlot` / `HoldSlot` (avoidance weights 0.5-teammates / 0). Interior crowding is servo+compression, as designed in arch §8.3.
- Determinism: booking resolves in sorted-handle order; slot assignment is a deterministic greedy bijection; internal paths are static structure data; no RNG enters the path.

## 3. Data model

### 3.1 Structure records (game-thread subsystem)

New `UBattleGarrisonSubsystem : UWorldSubsystem` owns plain-struct records (ADR-02 discipline: actors and authoring tools register plain data; sim never touches a UObject through it):

```cpp
enum class EGarrisonStructureType : uint8 { Building, Wall, Trench };

struct FGarrisonSlot            // one firing position (window, parapet, trench post)
{
    FVector2D Pos;              // m, planar
    float Z = 0.f;              // m (wall top > 0, trench < 0, building floor = 0)
    float YawDeg = 0.f;         // facing at the post (outward at parapets/windows)
    uint8 EntranceIndex = 0;    // which entrance routes to this slot
};

struct FGarrisonEntrance
{
    FVector2D DoorPos;          // m -- the choke point
    FVector2D QueuePos;         // m -- exterior approach point (planner goal)
    FVector2D AssemblyPos;      // m -- exterior re-form point after egress
    float ClearWidthM = 4.f;    // declared choke width (drives column morph)
};

struct FGarrisonStructureRecord
{
    uint32 StructureId = 0;     // assigned by the subsystem at registration
    EGarrisonStructureType Type = EGarrisonStructureType::Building;
    int32 MaxUnits = 1;         // booking capacity (ETW tooltip parity)
    FBox2D Footprint;           // m -- stamped impassable on the cost grid
    TArray<FGarrisonEntrance> Entrances;
    TArray<FBattlePathPoint3> EntryPaths;   // per entrance: QueuePos -> DoorPos -> distribution node (arc-length parameterized, carries Z)
    TArray<FGarrisonSlot> Slots;
};
```

`FBattlePathPoint3` = the existing `FBattlePathPoint` (Pos planar, S, Tangent) plus `float Z`. It lives on the pure-math seam; the fragment polyline `FBattlePathPoint` gains a `Z` field so internal paths travel losslessly (F1). Field paths carry Z = 0 -- the straight-path degeneracy of Phase 1 is preserved bit-for-bit.

Registration (`RegisterStructure`) stamps the cost grid: footprint cells impassable (reuses `StampImpassable`), entrance corridor cells set to a high-but-passable cost via a new `StampCost(BoxMin, BoxMax, Cost)` mirror (planner routes to doors naturally; no special-casing in A*). Wall/trench bodies stamp the same way; their walkways/slots exist only inside the record (not in the global grid -- the ground-level grid never knows about the wall top).

### 3.2 Booking ledger (same subsystem)

```cpp
struct FGarrisonBooking
{
    FUnitHandle Unit;
    uint32 StructureId = 0;
    uint8 EntranceIndex = 0;
    int32 SlotBase = 0;         // disjoint range [SlotBase, SlotBase+SlotCount)
    int32 SlotCount = 0;
};
```

- `TryBook(Handle, StructureId, out Booking)` -- capacity check (`MaxUnits`, total slots vs sum of rosters), disjoint-range allocation in **sorted-handle order** (deterministic; `GetSortedHandles()` discipline). Fails when full.
- `TryAcquireEntryLease(StructureId, EntranceIndex, Handle)` / `ReleaseEntryLease` -- at most one unit in `Ingress`/`Egress` per entrance at a time; others hold at queue points (B3).
- `ReleaseBooking(Handle)` -- on egress completion, unit death (hook the existing death-signal receiver), or unregister.
- Bookings are pure bookkeeping: reconstruction from spec is trivial (B.8 item 7, Iris-ready invariant).

### 3.3 Fragment additions (`BattleMovementFragments.h`)

All garrison state rides the existing per-unit shared fragment (F1) -- no new fragment types:

```cpp
// FUnitFormationSharedFragment additions:
EGarrisonMode  GarrisonMode;   // None, Garrisoning, Garrisoned, Exiting
FGarrisonPhase GarrisonPhase;  // None, Approach, Queue, Ingress, Occupy, Settled, Egress
uint32         GarrisonStructureId;
uint8          GarrisonEntranceIndex;
int32          GarrisonSlotBase;   // booked range, mirrored from the ledger
int32          GarrisonSlotCount;
int32          FieldFilesSaved;    // NumFiles before the column morph; restored on egress
float          GarrisonZ;          // current interior elevation target (cm)
FGarrisonSlotFragment Slots[MaxGarrisonSlotsPerUnit];  // booked-range mirror: Pos(cm), Z(cm), YawDeg
```

`MaxGarrisonSlotsPerUnit = 64` (fixture rosters are 40; 64 covers a 160-man unit at 2.5 files... sized to the largest planned roster). Cost: ~1 KB per unit shared fragment at 40 units = ~40 KB -- negligible against the §12 budgets, and zero when unused (fixed arrays are not touched until `Occupy`).

Per-entity: `FSlotTargetFragment` gains `float TargetZ` (cm). `FMovementStateFragment` and `FServoStateFragment` are unchanged.

## 4. Movement mechanics

### 4.1 Order surface

`FUnitOrder::EType` extends to `{ None, Move, Stop, Garrison, ExitGarrison }` with `uint32 StructureId` payload (thin-payload pattern preserved: plain data, converted at the fragment boundary). Registry APIs mirror `IssueMoveOrder`: `IssueGarrisonOrder(Handle, StructureId)` and `IssueExitGarrisonOrder(Handle)`, both bumping `OrderSerial`. The player-side rule (click-to-garrison) is a selection/UI concern (Phase 5 territory) -- the ExecBridge command surface is the v1 input: `Battle.Order.Garrison <unitIndex> <structureId>` and `Battle.Order.ExitGarrison <unitIndex>`.

A `Move` order received while garrisoned is **not** a direct field move: `UnitLocomotion` rewrites it as `Egress` followed by the pending move (the order is parked in `FUnitRuntimeState` as `PendingAfterEgress`). `Stop` while settled is a no-op. A `Garrison` order to the same structure while settled is a no-op; to a different structure it is `Egress` then re-approach.

### 4.2 Approach + queue

On order consumption (serial change, existing detector): plan a field path from the current anchor to the **queue point** of the least-loaded compatible entrance (deterministic pick: lowest entrance index whose lease is free, else lowest index overall). This is the existing A* path (`FindPath` + `Smooth` + `Resample`) -- footprint stamping makes the planner route around structures and to doors with zero planner changes (fact F6). Units arriving when the lease is taken or the structure is full enter `Queue`: hold formation at the queue point (existing `HoldSlot`), retry booking every 0.5 s (§12 reflow cadence reuse, `Battle.Garrison.QueueRetry`).

### 4.3 Ingress (the filing behavior)

On booking + lease acquisition: `Formation.Path` = the entrance's internal entry path (copied game-thread like `PolylineFromPlan`), `PathS = 0`, `PassLocked = 1` (column decision pre-committed -- the §8.6 commit rule), `FieldFilesSaved = NumFiles`, `NumFiles = clamp(floor(ClearWidthM / FileSpacing), 1, saved)`. From here the **existing machinery files the unit through the door**: anchor advances by `AdvanceAnchor`, path-frame layout projects column slots through the choke, servo+unstuck handle the interior crowding. Interior speed clamp: `UnitSpeedLimit` is additionally capped by `CommandedSpeed x InteriorSpeedFactor` (default 0.5, `Battle.Garrison.InteriorSpeedFactor`) whenever `GarrisonPhase` is `Ingress`/`Egress`. When the anchor reaches the distribution node (path end): `Occupy`.

### 4.4 Occupation

`Occupy` copies the booked slot range into `Formation.Slots`, `++Generation` (existing staleness channel), and `FormationSlotResolve` switches slot source: slot targets = structure slots (planar pos, yaw, Z), and the batched reflow runs `FGarrisonLayout::Assign` (distance-bounded nearest-man bijection over the booked range -- the k-file bound's analog, `Battle.Garrison.MaxSlotDisplacement`) instead of the formation assigner. Soldiers enter their posts via the normal `ManeuverToSlot -> HoldSlot` closure. When every live entity's slot error < `SlotReachEps`, the unit signals settled (up-path signal slot, arch §5.2) and enters `Settled`. Reflow stays active within the booked range (casualties close gaps post-by-post, same cadence) -- gated to `Occupy`/`Settled` phases only so reflow never fights path-frame filing (risk R-G4).

### 4.5 Egress

`Egress` mirrors ingress: `Formation.Path` = internal exit path (door -> assembly point), `NumFiles` = the saved column files (or the ingress column if `FieldFilesSaved` context differs), slots return to path-frame source so soldiers converge into the outgoing column from their posts. At the assembly point: restore `NumFiles = FieldFilesSaved`, release lease + booking, `GarrisonMode = None`, and either complete (`ExitGarrison`) or resume the parked `PendingAfterEgress` move (B6). The exterior re-form uses the normal compression recovery -- no special code.

## 5. Multi-unit structures

Capacity is expressed in units (`MaxUnits`) and enforced as disjoint slot ranges (`SlotBase`/`SlotCount`). Two units garrisoning one building: first books range A, ingress, settles; second books range B, queues at a per-entrance queue offset (queue points computed by `FGarrisonLayout::QueuePoints` at offsets behind `QueuePos`), enters on lease release. Occupancy state is visible via `Battle.Garrison.DumpStructures` (ExecBridge) and the debug overlay. Unit death mid-occupation: the death-signal receiver releases the booking; the remaining unit's range is untouched (ranges are disjoint by construction -- no reflow across unit boundaries).

## 6. Elevation model (walls and trenches)

The sim stays planar; Z is a **carried value, not a nav domain**:

- Internal paths carry per-point Z (stairs climb from 0 to wall-top height across the stair segment; trench ramps descend to negative channel depth). Slot Z during filing is interpolated along the path (`FGarrisonLayout::ZAtS`, monotone on stair fixtures -- T-G.5).
- `FormationSlotResolve` writes `TargetZ` from the slot (or path interpolation while filing); `IntegrateProcessor` replaces its Z-pin with a Z servo: `Z += clamp(TargetZ - Z, -MaxZRate*DT, +MaxZRate*DT)` when garrisoned, heightfield pin (flat 0) otherwise. MaxZRate bounds stair climb visually; planar X/Y integration is untouched.
- The state hash already folds entity Z at 1 cm (fact F9) -- wall-top and trench states hash correctly with no hash-format change; only the new per-unit garrison terms are appended (position-stable, §7 of the brief).

This is the minimal honest model for wall-top/trench movement. A true 3D traversal domain (multi-level pathing, ladder climbs) remains Open Decision 13.5 territory and is explicitly not built here.

## 7. Interaction with existing systems (exact touch points)

| System | Touch | Change |
|--------|-------|--------|
| `UnitLocomotionProcessor` | garrison FSM + booking + path adoption | Game-thread additions inside the existing per-unit loop; new order branches on serial change; interior speed clamp term; skip garrisoned units in the `UnitBodyQuery` OBB collection (R-G3) |
| `FormationSlotResolveProcessor` | slot-source branch + TargetZ + rest-facing | One branch on `GarrisonMode`; reflow assigner swap; write `TargetZ`; (see R-G5 for yaw authority) |
| `ServoProcessor` | none (weights unchanged) | No change required; interior crowding is servo+compression by design |
| `IntegrateProcessor` | Z servo | Replace Z-pin with garrison-aware Z servo; ~6 lines |
| `FBattleCostGrid` | `StampCost` | New method mirroring `StampImpassable` (deterministic, idempotent) |
| Registry / `FUnitOrder` | 2 order types + 2 issue APIs | Thin-payload extension; serial bump reused |
| `UBattleDebugSubsystem` | garrison overlay + metrics | Phase-colored unit markers, slot/occupancy draws, filing/queue-wait counters |
| State hash | garrison terms | Append per-unit mode/structure/phase/range terms after formation terms (position-stable) |
| Soak controller | optional garrison orders | Behind `Battle.Garrison.SoakOrders` so the T1.8 baseline is untouched until enabled |

## 8. ADR proposals (for the architecture decision log, owner approval pending)

| ADR | Decision | Rationale |
|-----|----------|-----------|
| ADR-18 | Garrison is a slot-source + path-source mode of the existing movement spine; no new processors, tags, or archetypes | Worker-safety requires data-in-fragments; uniform spine preserves the 6-archetype soak invariant; ADR-15 showed tag churn is real cost for zero benefit here |
| ADR-19 | Structure interiors are traversed via structure-owned internal paths; the global cost grid is stamped so field A* routes approaches to entrances only | Replicates ETW choke filing (B3) with zero planner changes; keeps the planar A* domain simple; internal paths are static data (deterministic) |
| ADR-20 | Elevation is carried Z on paths/slots with a bounded Z servo in Integrate; no layered nav domain | Walls/trenches need height only as a movement outcome (B4/B5); a 3D nav domain is Open Decision 13.5 scope and stays deferred |
| ADR-21 | Capacity = deterministic booking ledger (sorted-handle order, disjoint slot ranges, per-entrance entry lease) | ETW occupancy parity (B2); determinism without RNG; trivial reconstruction from spec (Iris-ready, B.8.7) |

## 9. Sequencing recommendation (owner decision requested)

**Recommendation: execute garrison as the next phase, before the engagement solver (Phase 2).** Folder `Docs/phases/phase-garrison/`, gate family `TG.x` so the engagement solver keeps its reserved `T2.x` numbering and no roadmap renumbering churn occurs.

Tradeoffs, honestly stated:

- **For garrison-first:** (1) the integration surface is the just-shipped Phase 1 movement code -- context is fresh and the assigner/locomotion touch points are exactly the code the team just hardened (44/44 unit tier green); (2) it is a small, low-risk phase (no new engine API territory, no new processors -- the brief's task plan is ~10 tasks); (3) battlefield structures become available as scenario geometry for every later phase (the engagement solver's S4/S5 maps, M2TW-feel demos); (4) building the `FormationSlotResolve` slot-source branch now is cheaper than after the engagement solver grows that processor; (5) the data model (structures, slots, bookings) gives Phase 2's engagement records a concrete shape to design against when assault mechanics arrive, even though combat hooks are out of scope here.
- **Against (engagement solver first):** the roadmap's risk-first principle ("riskiest systems first" -- Part A) points at the engagement solver as the signature-defining high-risk piece; garrison-first delays it by one phase. Counterpoint: the mitigation is the phase's small size; and garrison doubles as a generalization stress-test of the movement core (structure slots, interior crowding, elevation) which *de-risks* the platform the engagement solver runs on.

**Net:** garrison first, engagement solver immediately after, unchanged in scope.

## 10. Extension points for later phases (not built here, designed for)

- **Engagement solver (Phase 2):** slot tables already provide per-entity positions + facing for garrisoned defenders; `FGarrisonSlot` is the natural place to hang cover/arc metadata when combat hooks are greenlit (one struct member + a resolver read; no data-model rework). Booking records give the solver "garrisoned unit" classification for free.
- **Ranged stack (Phase 3):** wall-top slot Z feeds projectile origin heights; LOS sampling gains slot elevation without sim changes.
- **Morale (Phase 4):** "garrisoned" is a queryable unit state (fragment field) for morale contributions.
- **Representation (Phase 5):** entities at real interior positions make window visibility a rendering/occlusion concern only; crouch/fire poses key off `GarrisonPhase`.
- **Siege domain (Open Decision 13.5):** ladders/towers remain a separate movement domain; the internal-path mechanism generalizes if it is ever greenlit.
