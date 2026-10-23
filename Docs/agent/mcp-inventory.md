# MCP Inventory — MassBattle pre-phase-0 session (2026-10-23)

- **Server:** Epic official `ModelContextProtocol` plugin + `AllToolsets`, HTTP at `http://127.0.0.1:8000/mcp` (project `.mcp.json`; Saved config has `bAutoStartServer=True`, `ServerPortNumber=8000`, `bEnableToolSearch=True`).
- **Tool-search mode confirmed:** `tools/list` exposes exactly 3 meta-tools — `list_toolsets`, `describe_toolset`, `call_tool` (facts C1 verified).
- **Session access from this agent:** the ZCode session has no native `mcp__unreal-mcp__*` tools; all calls go through a small curl/JSON-RPC helper (initialize → `Mcp-Session-Id` header → `tools/call`). Serial, one call at a time (C3 honored).
- **Inventory taken:** immediately after Task 2's editor relaunch (editor was found closed at session start; single restart batching per §1.6 — see progress.md).

## Toolsets relevant to this brief

| Fully-qualified toolset name | One-line capability |
|---|---|
| `editor_toolset.toolsets.scene.SceneTools` | Load level, get current level, find/spawn (`add_to_scene_from_asset|class`)/remove actors, folders. **No new_level / save_all — use asset duplication + save_assets (see quirks).** |
| `editor_toolset.toolsets.actor.ActorTools` | Transforms, labels, tags, parent/child, add/remove components. |
| `editor_toolset.toolsets.primitive.PrimitiveTools` | add_cube/sphere/cylinder/cone components on an actor. |
| `editor_toolset.toolsets.asset.AssetTools` | find/load/save/delete/duplicate/move assets, `create_folder`, `exists`, dependencies. |
| `editor_toolset.toolsets.object.ObjectTools` | list/get/set/reset properties, get_class, search_subclasses. **Always `list_properties` before `set_properties`.** |
| `editor_toolset.toolsets.data_asset.DataAssetTools` | `create(folder_path, asset_name, asset_type)` — data assets (Mass pipeline proof target). |
| `editor_toolset.toolsets.programmatic.ProgrammaticToolset` | `execute_tool_script`: sandboxed Python `run()` calling `execute_tool("<FQ.ToolName>", json)`. **Imports limited to json/re/math/copy/datetime/time — `unreal` module NOT available.** |
| `AutomationTestToolset.AutomationTestToolset` | DiscoverTests → ListTests → RunTests/RunTestsByFilter → GetTestStatus/GetTestResults (wraps IAutomationControllerManager; the preferred Task 5 path). |
| `EditorToolset.EditorAppToolset` | Camera, selection, content browser, PIE start/stop, viewport capture. **No console-command tool.** |
| `EditorToolset.LogsToolset` | GetLogEntries (regex over session log — used as module-load evidence), Get/SetVerbosity, categories. |
| `ConfigSettingsToolset.ConfigSettingsToolset` | Inspect/edit config sections (SetSectionProperties/SaveSection). Brief mandates direct .ini file writes instead. |
| `ToolsetRegistry.AgentSkillToolset` | ListSkills/GetSkills/CreateSkill/UpdateSkill. |
| `editor_toolset.toolsets.blueprint.BlueprintTools` | Blueprint authoring (not needed pre-phase-0). |
| `editor_toolset.toolsets.material_instance.MaterialInstanceTools` / `.material.MaterialTools` | Material assets (not needed pre-phase-0). |
| `SlateInspectorToolset.SlateInspectorToolset` | Playwright-style editor UI automation (fallback of last resort). |

Full registry at time of writing: **49 toolsets** (Niagara, Sequencer ×8, ControlRig, PCG ×2, GAS ×3, StateTree, UMG, Physics, ChaosCloth, Dataflow, DataRegistry, GameFeatures, GameplayTags, WorldConditions, Conversation, LiveCoding, Plugin, SemanticSearch, …). Re-run `list_toolsets` after any plugin enable/editor restart (C2).

## AgentSkills present (AgentSkillToolset.ListSkills, Task 0 step 4)

21 skills, **all engine/plugin-registered, none project-specific to MassBattle**:
ChaosClothAssetConversion, DataflowGraphEditing, Skill_PCGGraphGeneration, Niagara ×8 (BlueprintInterop, Cleanup, CustomHLSL, Fundamentals, LevelPlacement, Optimization, Renderers), BlueprintBasics, DefaultOutdoorLighting, MaterialBasics, UnrealSkillBestPractices, Skill_InstantLevelOperations, PCGAssetZoo, PCGBiomeCore (+Refinement), PCGInstancingOnMeshActor, PCGMeshPartition, PCGShapeGrammarDefinition.

Task-3-adjacent: `DefaultOutdoorLightingSkill` (sky/sun setup) and `Skill_InstantLevelOperations` (batch actor placement) were noted; the brief's graybox spec is explicit and minimal, so generic defaults were followed.

## Operating quirks discovered (corrections to §1/§2)

1. **§1.4 Python fallbacks are not reachable through `ProgrammaticToolset.execute_tool_script`** — that sandbox forbids `import unreal` (whitelist: json/re/math/copy/datetime/time). Level creation/save must be done via asset tools (duplicate `/Engine/Maps/Templates/Template_Default` → save) instead of `les.new_level()`/`save_all_dirty_levels()`. `EditorLevelLibrary` deprecation note in §1.4 still correct.
2. `LogsToolset.GetLogEntries` defaults `category` to `"LogsToolset"`; pass `"category": ""` to search the whole session log, else it errors "Log category not found".
3. MCP responses arrive as plain JSON-RPC over streamable HTTP; long calls (automation runs) must respect a ~110 s practical timeout — poll `GetTestStatus` instead of holding one request open.
