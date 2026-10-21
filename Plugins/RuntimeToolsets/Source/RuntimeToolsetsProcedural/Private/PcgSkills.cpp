#include "PcgSkills.h"
#include "RuntimePcgToolset.h"

URuntimePcgGraphGenerationSkill::URuntimePcgGraphGenerationSkill()
{
	Description = TEXT("Apply whenever a task involves repeating patterns or rules over space: scattering vegetation or rocks, roads and fences along "
		"paths, filling areas, grids of buildings, or spatial computation (distances, subdivision, filtering points). Not for one-off placements.");
	Toolsets = {TEXT("pcg"), TEXT("spline")};
	Instructions = TEXT(R"SKILL(# PCG graph generation

PCG graphs turn input data (splines, volumes, actors) into points, filter and transform them, and spawn content.

## Quick path: scatter
For vegetation, rocks, fences, lamps along or inside a spline, pcg.scatter does the whole job with builder objects:
1. Shape the area with spline.create_spline (closed for areas, open for paths).
2. Build each object once as a template with build.create_assembly and keep it out of sight (e.g. 600 m below ground).
3. pcg.scatter with items (weights, scale ranges), spacing, min_distance, keep/noise_scale for clumps, and exclude
   (roads, rivers) with clearances. Same seed = same result; undo removes everything.
Typical values: forest spacing 600-800 cm, min_distance 400-500, keep 0.6-0.85, noise_scale 4000-6000;
roadside trees offset [900, 1600] from the road centre; fence posts spacing 400 with align true.

## Graphs node by node
Two kinds of nodes:
- **Primitive and example graphs** (subgraphs): higher-level building blocks. **Prefer these.** Inspect with
  pcg.get_graph_structure / pcg.get_graph_schema, add with pcg.add_subgraph_node.
- **Native nodes**: low-level engine nodes. Use when no primitive fits. Read pcg.get_native_node_schema before adding.

## Attributes
- Static attributes start with `$` ($Position, $Rotation, $Scale, $Density, $Seed, $Steepness). Prefer them.
- Dynamic attributes are created by nodes and stored in metadata; downstream nodes must read the exact name the
  upstream node wrote (set output names explicitly; nothing is inferred).
- Domains are prefixed with `@` (@Data for per-data attributes); default is per point.

## Reuse before rebuilding
- If an existing graph does the job, spawn it with parameter overrides (pcg.spawn_graph_instance params).
- If it is close, copy it (create a graph and add the original as a subgraph, then add nodes around it).
- Only build from scratch when nothing fits.

## Graph parameters
Expose what a user would tweak (densities, spacing, counts, sizes, meshes) with pcg.set_graph_params; each instance can
override them (pcg.set_graph_instance_params). Read a parameter inside the graph with the "Get Graph Parameter" node.
Asset arrays: type SoftObjectPath with array true.

## Workflow
1. Understand the request; open the relevant example graphs (pcg.get_graph_structure). Read several.
2. Gather schemas for every node you might use (pcg.get_native_node_schema, pcg.get_graph_schema).
3. Build: add each node with all its params in one pcg.add_node call; connect with pcg.connect_node_pins.
   Every graph needs a source ("Get Spline Data", "Get Actor Data", samplers) and something that produces output.
4. Run: pcg.spawn_graph_instance (on a volume, or on_actor = a spline node so "Get Spline Data" reads it), then
   pcg.execute_graph_instance after every structural or parameter change.
5. Inspect: pcg.get_node_data_view shows what any node outputs (counts, positions). Use it to find where points vanish.
6. If execution reports errors, tell the user what failed and propose a fix; do not rewire the graph blindly.

## Graph structure rules
- A spawner node (Static Mesh Spawner, Spawn Actor) is needed for anything to appear; pcg.scatter places instances itself.
- Samplers need bounds: surface/volume samplers on a volume, spline sampler on a spline.
- Self Pruning removes overlaps using point extents; set extents (Bounds Modifier) to the object's radius first.

## Available example and primitive graphs
{Graphs}

## Available native nodes
{Nodes}
)SKILL");
}

FString URuntimePcgGraphGenerationSkill::GeneratePrompt(const FString& InitialInstructions) const
{
	TArray<FString> Graphs = URuntimePcgToolset::ListGraphs();
	if (Graphs.Num() > 120)
	{
		const int32 More = Graphs.Num() - 120;
		Graphs.SetNum(120);
		Graphs.Add(FString::Printf(TEXT("... and %d more (pcg.list_graphs)"), More));
	}
	return InitialInstructions
		.Replace(TEXT("{Graphs}"), Graphs.Num() ? *FString::Join(Graphs, TEXT("\n")) : TEXT("(none packaged; enable the PCGPrimitives plugin for Epic's examples)"))
		.Replace(TEXT("{Nodes}"), *FString::Join(URuntimePcgToolset::ListNativeNodes(true), TEXT(", ")));
}
