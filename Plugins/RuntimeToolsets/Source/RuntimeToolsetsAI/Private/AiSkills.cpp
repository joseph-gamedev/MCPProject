#include "AiSkills.h"

URuntimeAiNavigationAndPerceptionSkill::URuntimeAiNavigationAndPerceptionSkill()
{
	Description = TEXT("Apply whenever characters or enemies must move around obstacles, see or hear the player, patrol, chase, search or run behavior trees.");
	Toolsets = {TEXT("ai"), TEXT("tags"), TEXT("logic")};
	Instructions = TEXT(R"SKILL(# AI: navigation, agents, perception

## Order of work
1. Build the walkable world first (floors, walls, obstacles with collision), then ai.build_navigation once. It covers
   everything in the scene plus a margin; the navmesh updates by itself when objects move later.
2. Turn enemies into agents: ai.create_agent (any builder object works; it keeps its node id). Or give a logic brain
   backend "ai" (logic.define_brain) and assign_brain does it for you.
3. Move with ai.move_to (waits for arrival) or let the brain or a behavior tree drive.
4. Perception: agents see pawns (the player) automatically; ai.make_perceivable for other objects they must see.
   Every gunshot, explosion or alarm should be an ai.report_noise so agents can react to sound.

## Numbers that feel right
| Setting | Guard | Hunter | Animal |
|---|---|---|---|
| max_speed (cm/s) | 250-350 walk, 450-600 chase | 500-650 | 300-800 |
| sight_radius (cm) | 1500-2500 | 3000-4000 | 1200-2000 |
| sight_half_angle (deg) | 45-60 | 60-75 | 80-110 |
| hearing_range (cm) | 1500-2500 | 2500-4000 | 2500-3500 |
| memory_seconds | 4-8 | 10-15 | 3-5 |
Lose-sight radius 15-25% above sight radius avoids flicker at the edge. Agents need gaps of at least twice their radius
to pass; doors 120 cm wide or more for human-sized agents.

## Diagnosis order
- Agent does not move: ai.get_navigation_status (navmesh? building? bounds cover the area?) -> ai.find_path between the
  agent and the goal (unreachable? partial?) -> ai.project_to_navigation on the goal (is it on walkable ground?).
- Agent does not react: ai.get_perception (is the target perceived? by which sense? how old?) -> sight blocked by
  geometry? -> target not a pawn and not made perceivable?
- Behavior tree misbehaves: ai.get_brain_state (active node, blackboard values), then ai.describe_behavior_tree.

## Tags
Use gameplay tags for state everyone must agree on (State.Stunned, State.Alerted, Team.Enemy): tags.add_actor_tags /
remove_actor_tags; logic rules test them with has_tag(target, 'State.Stunned'); GAS uses the same tags.
)SKILL");
	}

