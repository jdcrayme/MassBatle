# PR-2: Move the demo input mapping to an editor-owned IMC asset

| | |
|---|---|
| **Finds** | Audit B-4: keybinds and input geometry hard-coded in `ADemoPlayerController::SetupEnhancedInput` |
| **Blocks on** | Editor asset authoring (MCP: create `UInputAction`/`UInputMappingContext` assets), PIE keybind verification |
| **Owner** | Owner call on where the assets live (demo module content root) |

## Motivation

The programmatic-IMC approach in `SetupEnhancedInput()` was a bootstrap workaround (UE 5.8
deleted the legacy `BindKey` path; the run needed input without authoring assets). It has served
its purpose. What remains is ~50 lines of C++ that are pure DATA:

```
IA_PanFwd    <- W, Up
IA_PanBack  <- S, Down
IA_PanLeft  <- A, Left
IA_PanRight <- D, Right
IA_RotLeft  <- Q        IA_RotRight <- E
IA_Select   <- LMB      IA_Order <- RMB      IA_Halt <- H
IA_ZoomIn   <- WheelUp  IA_ZoomOut <- WheelDown
```

Retuning a keybind should not be a C++ commit. Per AGENTS §3 (owner decision 2026-10-25), the
demo module exists precisely for editor-authored UX.

## Proposed change

1. Author `IMC_Demo` + the 11 `IA_Demo*` actions as assets under `Content/MassBattle/Demo/Input/`
   (actions stay boolean-valued; the trigger semantics -- implicit Down for holds -- are what the
   C++ bindings already assume, so the assets reproduce current behavior exactly).
2. `ADemoPlayerController`: drop `MakeBoolAction`/`AddMapping`/`OwnedActions` and load the IMC
   asset by soft reference; keep the `UEnhancedInputComponent` bindings (they are behavior, not
   data) but resolve the actions by name from the loaded context.
3. The px thresholds (Click 5, DragGesture 12, PickRadius 250, SegmentGap 200 cm) stay as named
   `constexpr` for now -- they are tuning, not layout; promote them to a `UDemoInputTuning`
   data asset only if the owner asks (the audit already named them; B-8's `BodyLabelZCm` shows
   the pattern).

## Files

- New assets: `Content/MassBattle/Demo/Input/{IMC_Demo, IA_Demo_*}.uasset` (MCP authoring)
- Edit: `Source/MassBattleGame/DemoPlayerController.{h,cpp}` (delete ~70 lines of programmatic
  construction; add asset load + by-name action lookup)
- Edit: `DemoHUD.cpp` controls-hint block unchanged (the keybind display stays truthful either way)

## Evidence to gather on the UE machine

- PIE: every binding in the table above works exactly as the programmatic version did
  (pan/rotate-hold semantics, marquee, formation drag, halt, zoom)
- `Automation RunTests Battle.Func.OrderSmoke` green (order path exercises select + RMB)
- A keybind retune in the editor (e.g. pan-left A -> Z) takes effect with no recompile

## Rollback

Keep the programmatic path behind a one-release fallback: if `IMC_Demo` fails to load, log a
warning and fall back to the (temporarily retained) construction code; delete the fallback in
the following phase once PIE evidence is in.
