# Epic's editor toolsets vs. RuntimeToolsets

UE 5.8 ships AI-agent toolsets in `Engine/Plugins/Experimental/Toolsets` (PCG, Editor, Niagara, Physics, UMG, StateTree,
AIModule, GAS, GameplayTags, AutomationTest, ConfigSettings, ...) on top of `Experimental/ToolsetRegistry` and
`Experimental/ModelContextProtocol`. They are **editor-only** (headers + Python ship, no .cpp), so we cannot call them at
runtime, but they are the reference for API shape, guidance ("skills") and tests. Check them before designing a toolset.

## Already aligned
Meta / tool-search exposure, per-toolset version and description, async tools with cancel, schema snapshots,
atomic batches with rollback.

## Adopted so far
- **pcg**: real PCG graphs (Get Spline Data -> Spline Sampler or Create Surface From Spline -> Surface Sampler ->
  Transform Points -> Bounds Modifier -> Self Pruning), executed on demand, output points read back, the same node set
  Epic's PCG toolset lists as common. `pcg.scatter` is our high-level recipe on top.

## Gaps, by priority

### Framework
1. **Output schemas** for every tool (Epic derives them from return types; we have none) + optional `structuredContent`.
2. **Skills** instead of monolithic `get_guide`: `session.list_skills` (name -> "apply whenever ..." line) and
   `session.get_skills(names)`, with dynamic placeholders filled from live data (Epic: `UAgentSkill::GeneratePrompt`,
   `UPCGGraphGenerationSkill`). Follow Epic's authoring rules (`editor_toolset/skills/unreal_skill_best_practices.py`).
   Port `default_outdoor_lighting` (sun conventions, lux, exposure, screenshot-adjust loop) and Niagara's placement /
   interop / fundamentals skills.
3. **Descriptions**: document return shape, sort order, and "fails if ..." for every tool; MCP annotations
   (readOnly / destructive / idempotent) from our existing flags.
4. **Errors**: always list candidates on NotFound; name the expected type on type mismatches.
5. **Name filters** with `/regex/` semantics like `FToolset::SetNameFilters`.
6. Optional editor bridge registering our toolsets into ToolsetRegistry.

### Toolsets
7. **pcg (next)**: Epic-style graph tools - `create_graph`, `list_nodes`, `get_node_schema` (reflection), `add_node`
   (JSON params), `connect`, `remove_node`, `get_graph`, `execute`, `get_node_data` (inspect any node's output), and
   `pcg.get_guide` as a dynamic skill.
8. **reflection**: batch `get_properties` / `set_properties` (one undo step) / `reset_properties`, `describe_enum`.
9. **lighting**: Sky Atmosphere, Volumetric Cloud, sun source angle, exposure min/max; report exposure mode + cloud
   coverage in `get_environment`.
10. **vfx**: `get_summary`, `set_system`, auto-destroy one-shots, snap to ground.
11. **ui**: one-line-per-widget description with index-aligned refs; `add_child` / `move` / `wrap`.
12. **assets.find** over the cooked asset registry, with discovery groups in settings.
13. **Engine gameplay systems (optional modules)**: Gameplay Tags (`tags.*`), GAS (`gas.*` inspect + apply effects /
    activate abilities / send events), AI (`ai.run_behavior_tree`, blackboard, `move_to`, perception, noise),
    StateTree runtime (`send_event`, active states). Let logic brains optionally use AIController + navmesh + perception.

### Tests
14. A failure case per tool; output-schema snapshots; skill tests (every tool a skill names exists).
