#include "StateTreeSkills.h"

URuntimeStateTreeSkill::URuntimeStateTreeSkill()
{
	Description = TEXT("Apply when a task involves the project's StateTree assets: running a designed behaviour (guard, door, boss phases, "
		"quest flow) on an actor, sending it events, or checking which state it is in.");
	Toolsets = {TEXT("statetree")};
	Instructions = TEXT(R"SKILL(# StateTree (statetree toolset)

## When to use
- The project already has StateTree assets (statetree.list_state_trees is not empty) and the task is to run, drive or
  debug them on actors at runtime.
- StateTree assets cannot be created or edited at runtime (compilation is editor-only). For new behaviour built from
  scratch use logic.define_brain / logic.assign_brain (enemies) or logic.define_rules (game rules) instead.

## Workflow
1. statetree.list_state_trees (filter by name), then statetree.describe_state_tree: schema, states (paths such as
   Root/Combat/Attack), tasks, enter conditions, transitions and the event tags the tree listens for ("events").
2. statetree.run_state_tree on the actor. Adds a StateTreeComponent if needed and starts the logic. The schema must be
   StateTreeComponentSchema (AI schemas need an AI controller). Undo removes the component.
3. statetree.send_event with a tag from "events" (payload only when the transition declares a payload type).
   Transitions fire on the next tree update, so call statetree.get_active_states after a frame or two.
4. statetree.get_active_states shows the run status and active state paths; statetree.stop_state_tree and
   statetree.restart_state_tree control it.

## Diagnose in this order
1. Not ready to run: the asset is not compiled; fix it in the editor.
2. Does not start: missing context data (the actor lacks a component the schema needs) or the root enter conditions fail.
3. Event ignored: wrong tag (compare with describe "events"), the tree is in a state with no transition for it, or the
   transition needs a payload type.
4. Succeeded/Failed status: the tree finished; restart it.
)SKILL");
}
