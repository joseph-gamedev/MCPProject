#include "RuntimeLogicToolset.h"
#include "LogicJsonHelpers.h"
#include "LogicRuntime.h"
#include "LogicTuning.h"
#include "RuntimeCommand.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetsCore.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace LogicToolsetPrivate
{
	const TCHAR* GuideText = TEXT(R"GUIDE(
MAKE A PLAYABLE GAME WITHOUT CODE. The game runs by itself once started; you only define it.

1. Build the scene with build/actors/scene tools. Give clickable objects names or tags (tags:["cell"]).
   Attach data to them: logic.set_data {target, data:{index:4}} -> rules read event.data.index.
2. Variables: logic.define_vars {vars:{board:["","","","","","","","",""], turn:"X", winner:"", moves:0}}.
   These are the starting values; logic.start (and the restart action) resets to them.
3. Rules: logic.define_rules {rules:[{name, on, target|key|filter, condition, actions}]}
   on: start | click | right_click (target: node id/name/path or "tag:cell") | key (key:"R", "SpaceBar", "One")
       | button (filter: ui name) | timer (filter: timer name) | var_changed (filter: variable) | event (filter: emit name) | tick
   condition: expression; the rule runs only when true.
4. Actions (run in order):
   {"set":"turn","value":"turn == 'X' ? 'O' : 'X'"}       value is an EXPRESSION: quote text: "'X'"
   {"set_item":"board","index":"event.data.index","value":"turn"}   {"push":"list","value":"..."}
   {"let":"i","value":"..."} local   {"if":"expr","then":[...],"else":[...]}   {"for":"i","in":"range(9)","do":[...]}
   {"call":"build.create_primitive","args":{"shape":"box","location":"=event.location","name":"piece_{moves}"},"as":"made"}
      args: "=expr" -> value of expr, "text {expr}" -> interpolated. Objects created by rules are removed on restart.
      Any tool works (materials.set_material, scene.move_node, ui..., vfx..., physics...). Not logic.* tools.
   {"emit":"won","data":{"who":"=turn"}}   {"timer":"ai","delay":"0.6","repeat":false}   {"cancel_timer":"ai"}
   {"set_ui":"status","text":"...","visible":"expr"}   {"log":"moved to {event.data.index}"}   {"restart":true,"keep":["score"]}   {"stop":true}
5. Expressions: numbers, 'text', true/false/null, lists [1,2], + - * / % == != < <= > >= && || ! and or not, a ? b : c,
   list[i], map.field, event.target/name/data/tags/location, time. Functions: len abs floor ceil round sqrt min max rand
   rand_int(a,b) str num list(n,fill) range(n) contains index_of count indices pick sum join set(list,i,v) keys upper lower
   grid_lines(rows,cols,k) line_winner(board,lines,empty) winning_line(...) winning_move(board,lines,player,empty).
6. UI: logic.define_ui {items:[{name:"status",kind:"text",text:"{winner == '' ? turn + ' to move' : winner + ' wins!'}",
   anchor:[0.5,0.06]}, {name:"restart",kind:"button",text:"Restart",anchor:[0.5,0.94]}]}  (text updates live)
7. logic.set_input_mode {mode:"cursor", lock_camera:true} for click games; then camera.set_camera to frame the board.
8. logic.start. Test it yourself: logic.fire_event {on:"click", target:"cell_4"} / {on:"button", name:"restart"} /
   {on:"key", key:"R"}, logic.step {seconds} for timers, logic.get_state for vars + event log + errors.
   Fix rules until a full game (win, draw, restart) works, then tell the user how to play.
)GUIDE" R"GUIDE(PLATFORMERS / ACTION GAMES:
   logic.spawn_player {location:[x,0,z] (feet), speed, jump, body:"<node>" (optional: your own character model, feet at
   z=0, facing +X; default: a cap-and-overalls hero), kill_z}. Side view: the level runs along +X in the plane Y=0, the
   camera follows from +Y (put background scenery at negative Y). Controls: A/D or arrows to run, Shift to sprint,
   Space/W/Up to jump (hold for higher), gamepad works too. Do not use camera tools while a player exists.
   Touch: on:"touch", target:"tag:coin" fires when the player first overlaps it; event.data.from_above (stomp),
   event.data.from_below (head bump), event.data.side. on:"fall" fires below kill_z (without a fall rule: respawn).
   logic.set_solid {targets:["tag:coin","tag:enemy"], solid:false} so pickups and enemies don't block the player.
   logic.set_motion {targets:["tag:goomba"], motion:{type:"patrol", axis:[1,0,0], distance:500, speed:120, face:true}}
      types: patrol | path {points:[[0,0,0],[0,0,300]], loop} | bob {amplitude, period}; any: spin:[0,180,0] deg/s, offset.
   Actions: {"hide":"event.target"} / {"show":...} (restored on restart), {"player":"bounce","value":"900"},
   {"player":"respawn"}, {"player":"checkpoint"}, {"player":"freeze"}, {"player":"launch","value":"[0,0,1500]"}.
   Expressions: player.x / player.z / player.grounded / player.velocity.
   Typical rules: coin touch -> hide + coins+1; enemy touch from_above -> hide enemy + bounce + score; enemy touch
   otherwise -> lives-1 + respawn (lives==0 -> freeze + game over text); fall -> same as hit; flag touch -> win + freeze;
   checkpoint touch -> {"player":"checkpoint"}; key R or button -> restart (keep high score).
)GUIDE" R"GUIDE(TOP-DOWN / SHOOTERS: logic.spawn_player {mode:"topdown", location, speed} -> WASD moves on the ground, the hero faces
   the mouse cursor, an overhead camera follows (camera_distance, camera_pitch). Left mouse / F / right trigger fires.
   logic.define_health {targets:["player"] or ["tag:enemy"], max, invulnerable (s after a hit), resistances:{fire:0.5},
      on_death: hide|squash|release|respawn|freeze|none, points_var, points}. Rules: on:"damaged"/"died" target:"player"
      or "tag:enemy" with event.amount/health/max/type/instigator/location. Expressions: health.player, hp(id), alive(id).
   logic.define_hitzone {targets:["tag:lava"], damage, damage_max, type, affects:"player"|"tag:x"|"any", mode:"enter"|"stay",
      interval, knockback} -> automatic damage on overlap (spikes, lava, explosions, melee areas).
   logic.define_weapon {name, mode:"projectile"|"hitscan", damage, rate (shots/s), auto, burst, burst_gap, magazine, reload,
      ammo_var (reserve), per_shot, spread (deg), spread_mode:"random"|"fan", speed, lifetime, range, knockback, hits:
      "tag:enemy"|"player"|"any", bullet:{size, color} or pool:"name"}. logic.equip {owner:"player" or "tag:turret", weapon}.
   Actions: {"fire":"event.target","at":"pos('player')"} (enemies shoot), {"damage":"event.target","amount":"3"},
   {"heal":"'player'","amount":"25"}. Events fired / reloaded. Functions: pos(id), hp(id), alive(id), nodes('tag:x'),
   dist(a,b). Typical enemy fire rule: on timer repeat -> for e in nodes('tag:enemy') -> if alive(e) && dist(pos(e),
   pos('player')) < 1500 -> {"fire":"e","at":"pos('player')"}. HUD: "HP {health.player}  AMMO {ammo.player}/{reserve}".
)GUIDE" R"GUIDE(AI BRAINS (state machines): logic.define_brain {name, start, vision:{radius, angle, eye_height, show_cone}, hearing:
   {radius} (hears the player's shots), turn_speed, states:[{name, move: stop|patrol|chase|strafe|flee|to_last_known|home|
   wander, speed, keep_distance, face: move|target|look_around, fire: true (shoot while seeing the player), icon: "!"|"?",
   transitions:[{when:"sees", to:"alert"}, ...]}]}. Transition expressions can use sees, heard, distance, time_in_state,
   since_seen, at_goal, at_home, has_last_known, health, health_pct, player_alive, self + all variables.
   logic.assign_brain {targets:["tag:guard"], brain, patrol:{axis:[1,0,0], distance:600} or {points:[[0,0,0],[600,0,0]]}}
   (draws the vision cone: yellow calm, orange searching, red seeing you). Rules: on:"state" filter:"<state>" target:tag.
   Classic guard: patrol -(sees)-> alert (stop, face target, icon !, 0.5 s) -> attack (strafe/chase, fire, keep_distance
   500) -(since_seen > 2)-> search (to_last_known, icon ?) -(at_goal && time_in_state > 3)-> return (home) -> patrol;
   any calm state -(heard)-> search.
GAME HUD: logic.set_hud {enemy_bars, alert_icons, damage_numbers, hit_flash, low_health} draws health bars over damaged
   enemies, ! / ? icons, floating damage numbers, a red flash when hurt. define_ui kind:"bar" {value:"health.player",
   max:"10", fill:[r,g,b,a], size:[w,h], ghost:true, text:"HP {health.player}"}; anchors near 0/1 snap to screen edges.
FEEDBACK (juice): logic.define_feedback {name, steps:[{type: shake|flash|freeze|slowmo|text|tween|spawn|emit, delay,
   duration, intensity, scale, color, text, pool, ...tween fields}]}; play with {"feedback":"name"} or attach
   hit_feedback/death_feedback to define_health and feedback to define_weapon.
)GUIDE" R"GUIDE(FLIGHT / DOGFIGHT: logic.spawn_player {mode:"flight", location, yaw, speed (cruise), min_speed, max_speed (boost), turn,
   roll_rate, body:"<jet model node>" (nose +X), secondary:"<missile weapon>", camera_distance, camera_height, fov,
   auto_spawn:false (spawn later with {"player":"spawn"})}. Mouse/arrows steer, A/D roll, W/S throttle, Shift boost,
   LMB guns, RMB missiles. Crashing fires on:"crash" and deals crash_damage. player.speed/altitude/throttle/lock.
   Weapons: homing (deg/s turn), lock_range, lock_angle make missiles. Shots inherit the shooter's speed.
   Enemy jets: define_brain {..., flight:{speed, turn_rate, min_altitude, orbit_radius}} â€” move patrol=orbit,
   chase=lead pursuit with break-off, flee=evade; fire when the player is within fire_angle / fire_range.
   set_hud {markers:true} draws target brackets, distance, the missile lock and a gun reticle.
   Change the player at runtime: {"player":"set","fields":{"body":"=jets[sel].body","speed":"=..."}} (new body
   respawns), {"player":"spawn"} / {"player":"remove"}, {"health":"'player'","max":"...","value":"..."},
   {"weapon":"guns","set":{"damage":"=..."}}.
SPAWNERS / WAVES: logic.define_spawner {name, pool, points:[[x,y,z]] or around_player:{radius:[a,b], height:[a,b]},
   interval (s or [min,max]), max_alive, total, waves:[{count, interval, max_alive}], wave_gap, start_delay, autostart,
   face_player}. Events wave_start / wave_cleared / waves_complete / spawned (filter: spawner). Action
   {"spawner":"name","op":"start|stop|reset"}. Spawned pool members get fresh health and brains; use on_death:"release".
LOOT: logic.define_loot {name, entries:[{pool, weight}], count:[min,max], scatter}; define_health {loot:"name"} drops
   on death; action {"drop_loot":"name","at":"..."}.
INTERACT: logic.define_interactable {targets, prompt:"Open door", key:"E", radius, once, cooldown, requires:"has('key')",
   locked_prompt:"Locked"} -> on:"interact" / "interact_denied" with target. Inventory: {"give":"'key'","amount":"1"},
   {"take":...}, has('key'), inventory.key. Teleport: {"player":"teleport","value":"[x,y,z]"}.
SAVES: {"save":"slot","vars":["credits","upgrades"]}, {"load":"slot"} (missing slot = nothing happens).
ABILITIES: spawn_player {dash:{speed, duration, cooldown, key, invulnerable}} (on:"dash"), grid:<cell cm> + step_time
   for cell-by-cell movement in top-down mode.
TWEENS (animation): action {"tween":"event.target","move":[0,0,40],"duration":0.12,"ease":"out_quad","loop":"yoyo"}
   fields: location|move (relative), rotation|rotate (relative), scale (number or [x,y,z]), color [r,g,b], duration, delay,
   ease (linear in_quad out_quad in_out_quad in_cubic out_cubic in_out_cubic in_sine out_sine in_out_sine in_back out_back
   in_out_back out_elastic in_bounce out_bounce), loop (once|yoyo|loop|pingpong), repeat, then (hide|show|release),
   emit (event name when done), add (stack with other tweens). Values may be "=expr". Also the logic.tween tool (any time).
   Examples: stomp squash {"tween":"event.target","scale":[1.4,1.4,0.2],"duration":0.25,"ease":"out_back","then":"hide"};
   coin collect {"tween":"event.target","move":[0,0,150],"scale":0.2,"duration":0.3,"then":"hide"};
   block bump {"tween":"event.target","move":[0,0,30],"duration":0.08,"loop":"yoyo"}.
)GUIDE" R"GUIDE(POOLS (reuse objects instead of creating them): logic.define_pool {name:"pop", template:"<node>", size:8} hides the
   template and pre-makes copies. Action {"spawn":"pop","at":"[x,y,z] expr","as":"p"} shows a free copy there (p.id);
   {"release":"p.id"} or tween then:"release" returns it. Restart returns all. Use for coins from blocks, bullets, effects.
Computer opponent: on var_changed turn (filter:"turn", condition:"turn == 'O'"), start a short timer, then pick
   winning_move(board,lines,'O','') or block winning_move(board,lines,'X','') or 4 or pick(indices(board,'')).
)GUIDE");

	FLogicWorld& LogicFor()
	{
		return FLogicManager::Get().For(RuntimeToolCall::GetWorld());
	}

	/** Lower-case runtime spelling of an enum value ("OutQuad" -> "out_quad", "RightClick" -> "right_click"). */
	template <typename TEnum>
	FString LogicEnumString(TEnum Value)
	{
		return RuntimeToolReflection::ToSnakeCase(StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value)));
	}

	/** Whether the caller passed an optional argument (AutoCreateRefTerm arguments have no default to fill in). */
	bool LogicArgPassed(const TCHAR* Arg)
	{
		const FToolInvocation* Inv = RuntimeToolCall::Current();
		return Inv && Inv->Has(Arg);
	}

	/**
	 * The call's arguments as a definition object (defaults filled, minus Exclude, without nulls and empty strings).
	 * Definitions are stored in this shape: it is what logic.export returns and what the runtime reads, and keeps
	 * "not given" distinct from any value for settings whose default depends on context.
	 */
	TSharedRef<FJsonObject> LogicPassedArgs(std::initializer_list<const TCHAR*> Exclude)
	{
		TSharedRef<FJsonObject> Def = MakeShared<FJsonObject>();
		const FToolInvocation* Inv = RuntimeToolCall::Current();
		if (!Inv)
		{
			return Def;
		}
		const TSharedPtr<FJsonObject> Copy = RuntimeJson::Parse(RuntimeJson::Write(Inv->GetArgs()));
		if (!Copy.IsValid())
		{
			return Def;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Copy->Values)
		{
			bool bSkip = !P.Value.IsValid() || P.Value->IsNull() || (P.Value->Type == EJson::String && P.Value->AsString().IsEmpty());
			for (const TCHAR* E : Exclude)
			{
				bSkip |= P.Key == E;
			}
			if (!bSkip)
			{
				Def->SetField(P.Key, P.Value);
			}
		}
		return Def;
	}

	template <typename TEnum>
	void LogicNormalizeEnum(const TSharedRef<FJsonObject>& Def, const TCHAR* Field, TEnum Value)
	{
		if (Def->HasField(Field))
		{
			Def->SetStringField(Field, LogicEnumString(Value));
		}
	}

	template <typename TMapType>
	FString LogicKeyList(const TMapType& Map)
	{
		TArray<FString> Keys;
		for (const auto& P : Map)
		{
			Keys.Add(P.Key);
		}
		Keys.Sort();
		return Keys.Num() > 0 ? FString::Join(Keys, TEXT(", ")) : FString(TEXT("none"));
	}

	/** Applies an edit to the definitions as one undoable step (undo restores the previous definitions). */
	bool EditLogicDefinitions(const FString& Label, TFunctionRef<bool(FLogicWorld&, FString&)> Edit)
	{
		UWorld* World = RuntimeToolCall::GetWorld();
		FLogicWorld& L = FLogicManager::Get().For(World);
		const TSharedRef<FJsonObject> Before = L.ExportDefinitions();
		FString Err;
		if (!Edit(L, Err))
		{
			FString Ignore;
			L.ImportDefinitions(Before, Ignore);
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err, TEXT("See logic.get_guide for the format"));
			return false;
		}
		const TSharedRef<FJsonObject> After = L.ExportDefinitions();
		TWeakObjectPtr<UWorld> WeakWorld = World;
		auto Apply = [WeakWorld](const TSharedRef<FJsonObject>& Defs)
		{
			FLogicWorld* Lw = WeakWorld.IsValid() ? FLogicManager::Get().Find(WeakWorld.Get()) : nullptr;
			FString E;
			return Lw && Lw->ImportDefinitions(Defs, E);
		};
		return RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(Label,
			[Apply, After](FRuntimeCommandContext&, FString&) { return Apply(After); },
			[Apply, Before](FRuntimeCommandContext&) { return Apply(Before); }));
	}

	bool LogicRequireTargets(const TArray<FString>& Targets)
	{
		if (Targets.Num() == 0)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("targets is empty"), TEXT("Pass node ids, paths, names or \"tag:x\""));
			return false;
		}
		return true;
	}

	bool LogicRequireName(const FString& Name, const TCHAR* What)
	{
		if (Name.TrimStartAndEnd().IsEmpty())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s name is empty"), What));
			return false;
		}
		return true;
	}

	/** The scene node of an actor argument; fails the call if it is not one. */
	const FRuntimeSceneNode* LogicNodeArg(AActor* Actor, const TCHAR* Arg)
	{
		const FRuntimeSceneNode* N = Actor ? RuntimeToolCall::NodeOf(Actor) : nullptr;
		if (!N)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s is not a scene node"), Arg),
				TEXT("Use a node id or path from build/scene/actors tools"));
		}
		return N;
	}

	template <typename TStruct>
	TStruct LogicStructFromJson(const TSharedPtr<FJsonObject>& Json)
	{
		TStruct Out;
		RuntimeToolReflection::FConvertContext Ctx;
		Ctx.World = RuntimeToolCall::GetWorld();
		Ctx.Graph = RuntimeToolCall::GetGraph();
		Ctx.Outer = GetTransientPackage();
		FString Err;
		if (Json.IsValid() && !RuntimeToolReflection::JsonToStruct(Json, TStruct::StaticStruct(), &Out, Ctx, Err))
		{
			UE_LOG(LogTemp, Warning, TEXT("logic: could not convert %s: %s"), *TStruct::StaticStruct()->GetName(), *Err);
		}
		return Out;
	}

	FRuntimeToolJson LogicVarsJson(const TMap<FString, FLogicValue>& Vars)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		for (const TPair<FString, FLogicValue>& P : Vars)
		{
			J->SetField(P.Key, P.Value.ToJson());
		}
		return FRuntimeToolJson::Make(TSharedPtr<FJsonObject>(J));
	}

	TMap<FString, double> LogicHealthMap(const FLogicWorld& L)
	{
		TMap<FString, double> Out;
		const TSharedPtr<FJsonValue> J = L.HealthMap().ToJson();
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (J.IsValid() && J->TryGetObject(O))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*O)->Values)
			{
				Out.Add(P.Key, P.Value->AsNumber());
			}
		}
		return Out;
	}

	FString LogicControlsFor(const FString& Mode)
	{
		if (Mode == TEXT("topdown"))
		{
			return TEXT("WASD: move, mouse: aim, left mouse / F / right trigger: fire");
		}
		if (Mode == TEXT("flight"))
		{
			return TEXT("Mouse or arrows: steer, A/D: roll, W/S: throttle, Shift: boost, left mouse: guns, right mouse / G: secondary weapon");
		}
		return TEXT("A/D or arrows: run, Shift: sprint, Space/W/Up: jump (hold = higher)");
	}

	FLogicPlayerState LogicPlayerState(const FLogicWorld& L)
	{
		FLogicPlayerState P = LogicStructFromJson<FLogicPlayerState>(L.PlayerJson());
		if (L.PlayerDef.IsValid())
		{
			P.Mode = Str(L.PD(), TEXT("mode"), TEXT("side"));
			P.Controls = LogicControlsFor(P.Mode);
			FString Reload;
			if (L.PD()->TryGetStringField(TEXT("reload_key"), Reload) && !Reload.IsEmpty())
			{
				P.Controls += TEXT(", ") + Reload + TEXT(": reload");
			}
		}
		return P;
	}

	FLogicGameState LogicState(const FLogicWorld& L, int32 LogLimit)
	{
		FLogicGameState S = LogicStructFromJson<FLogicGameState>(L.GetState(LogLimit));
		S.Player = LogicPlayerState(L);
		return S;
	}

	bool LogicRequireRunning(const FLogicWorld& L)
	{
		if (!L.IsRunning())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("logic is not running"), TEXT("Call logic.start first"));
			return false;
		}
		return true;
	}

	/** Resolves target specs to node ids inside a definition edit. */
	bool LogicResolveTargets(const FLogicWorld& L, const TArray<FString>& Targets, TArray<FString>& OutIds, FString& Err)
	{
		for (const FString& T : Targets)
		{
			TArray<FString> Ids;
			if (!L.ResolveNodes({T}, Ids, Err))
			{
				return false;
			}
			OutIds.Append(Ids);
		}
		return true;
	}
}

using namespace LogicToolsetPrivate;

// ---- Guide ----------------------------------------------------------------------------------------------------------

void URuntimeLogicToolset::GetGuide(FString& Guide)
{
	Guide = FString(GuideText).TrimStartAndEnd();
}

// ---- Variables ------------------------------------------------------------------------------------------------------

FLogicVarsResult URuntimeLogicToolset::DefineVars(const TMap<FString, FRuntimeToolJson>& Vars, bool bReplace)
{
	FLogicVarsResult Out;
	if (EditLogicDefinitions(TEXT("define vars"), [&](FLogicWorld& L, FString& Err)
	{
		if (bReplace)
		{
			L.InitialVars.Reset();
			L.Vars.Reset();
		}
		for (const TPair<FString, FRuntimeToolJson>& P : Vars)
		{
			if (P.Key.IsEmpty() || !FChar::IsAlpha(P.Key[0]) || P.Key == TEXT("event") || P.Key == TEXT("time"))
			{
				Err = FString::Printf(TEXT("bad variable name '%s' (letters, digits, _; not 'event' or 'time')"), *P.Key);
				return false;
			}
			const FLogicValue V = FLogicValue::FromJson(P.Value.Value.IsValid() ? P.Value.Value : MakeShared<FJsonValueNull>());
			L.InitialVars.Add(P.Key, V);
			L.Vars.Add(P.Key, V.DeepCopy());
		}
		return true;
	}))
	{
		Out.Vars = LogicVarsJson(LogicFor().InitialVars);
	}
	return Out;
}

FLogicValueResult URuntimeLogicToolset::SetVar(const FString& Name, const FRuntimeToolJson& Value, const FString& Expr)
{
	FLogicValueResult Out;
	FLogicWorld& L = LogicFor();
	if (!L.Vars.Contains(Name))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no variable '%s' (defined: %s)"), *Name, *LogicKeyList(L.Vars)),
			TEXT("Define it with logic.define_vars"));
		return Out;
	}
	FLogicValue V;
	if (LogicArgPassed(TEXT("expr")))
	{
		FString Err;
		V = L.Evaluate(Expr, Err);
		if (!Err.IsEmpty())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("expr: ") + Err);
			return Out;
		}
	}
	else if (Value.IsSet())
	{
		V = FLogicValue::FromJson(Value.Value);
	}
	else
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("pass value (JSON) or expr (an expression)"), TEXT("e.g. {\"name\":\"score\",\"expr\":\"score + 5\"}"));
		return Out;
	}
	L.SetVar(Name, V, true);
	Out.Value = FRuntimeToolJson::Make(L.Vars.FindRef(Name).ToJson());
	return Out;
}

FLogicEvalResult URuntimeLogicToolset::Eval(const FString& Expr)
{
	FLogicEvalResult Out;
	FString Err;
	const FLogicValue V = LogicFor().Evaluate(Expr, Err);
	if (!Err.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err, TEXT("See logic.get_guide for the expression syntax and functions"));
		return Out;
	}
	Out.Value = FRuntimeToolJson::Make(V.ToJson());
	Out.Type = V.TypeName();
	return Out;
}

// ---- Rules ----------------------------------------------------------------------------------------------------------

FLogicDefineRulesResult URuntimeLogicToolset::DefineRules(const TArray<FRuntimeToolJson>& Rules, bool bReplace)
{
	FLogicDefineRulesResult Out;
	if (EditLogicDefinitions(TEXT("define rules"), [&](FLogicWorld& L, FString& Err)
	{
		if (bReplace)
		{
			L.Rules.Reset();
		}
		for (int32 i = 0; i < Rules.Num(); ++i)
		{
			const TSharedPtr<FJsonObject> O = Rules[i].AsObject();
			FLogicRule Rule;
			if (!O.IsValid())
			{
				Err = FString::Printf(TEXT("rules[%d] must be an object {name, on, ...}"), i);
				return false;
			}
			if (!FLogicRule::FromJson(O, Rule, Err))
			{
				return false;
			}
			const int32 Existing = L.Rules.IndexOfByPredicate([&Rule](const FLogicRule& R) { return R.Name == Rule.Name; });
			if (Existing != INDEX_NONE)
			{
				L.Rules[Existing] = Rule;
			}
			else
			{
				L.Rules.Add(Rule);
			}
			Out.Defined.Add(Rule.Name);
		}
		return true;
	}))
	{
		Out.Rules = LogicFor().Rules.Num();
	}
	return Out;
}

void URuntimeLogicToolset::RemoveRules(const TArray<FString>& Names)
{
	EditLogicDefinitions(TEXT("remove rules"), [&](FLogicWorld& L, FString& Err)
	{
		for (const FString& N : Names)
		{
			if (L.Rules.RemoveAll([&N](const FLogicRule& R) { return R.Name == N; }) == 0)
			{
				TArray<FString> Known;
				for (const FLogicRule& R : L.Rules)
				{
					Known.Add(R.Name);
				}
				Err = FString::Printf(TEXT("no rule '%s' (rules: %s)"), *N, Known.Num() ? *FString::Join(Known, TEXT(", ")) : TEXT("none"));
				return false;
			}
		}
		return true;
	});
}

FLogicRuleList URuntimeLogicToolset::ListRules()
{
	FLogicRuleList Out;
	for (const FLogicRule& R : LogicFor().Rules)
	{
		Out.Rules.Add(FRuntimeToolJson::Make(TSharedPtr<FJsonObject>(R.ToJson())));
	}
	return Out;
}

FLogicNodeRef URuntimeLogicToolset::SetData(AActor* Target, const FRuntimeToolJson& Data)
{
	FLogicNodeRef Out;
	const FRuntimeSceneNode* N = LogicNodeArg(Target, TEXT("target"));
	if (!N)
	{
		return Out;
	}
	const TSharedPtr<FJsonObject> Obj = Data.AsObject();
	if (!Obj.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("data must be an object"), TEXT("e.g. {\"index\": 4}"));
		return Out;
	}
	const FString Id = N->Id;
	Out.Name = N->Name;
	if (EditLogicDefinitions(TEXT("set node data"), [&](FLogicWorld& L, FString&)
	{
		L.NodeData.Add(Id, FLogicValue::FromJson(MakeShared<FJsonValueObject>(Obj)));
		return true;
	}))
	{
		Out.Id = Id;
	}
	return Out;
}

// ---- UI -------------------------------------------------------------------------------------------------------------

FLogicDefineUiResult URuntimeLogicToolset::DefineUi(const TArray<FRuntimeToolJson>& Items, bool bReplace)
{
	FLogicDefineUiResult Out;
	if (EditLogicDefinitions(TEXT("define ui"), [&](FLogicWorld& L, FString& Err)
	{
		if (bReplace)
		{
			L.Ui.Reset();
		}
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			const TSharedPtr<FJsonObject> O = Items[i].AsObject();
			FLogicUiItem U;
			if (!O.IsValid())
			{
				Err = FString::Printf(TEXT("items[%d] must be an object {name, kind, text, ...}"), i);
				return false;
			}
			if (!FLogicUiItem::FromJson(O, U, Err))
			{
				return false;
			}
			const int32 Existing = L.Ui.IndexOfByPredicate([&U](const FLogicUiItem& X) { return X.Name == U.Name; });
			if (Existing != INDEX_NONE)
			{
				L.Ui[Existing] = U;
			}
			else
			{
				L.Ui.Add(U);
			}
		}
		L.RefreshUi();
		return true;
	}))
	{
		Out.Items = LogicFor().Ui.Num();
		const UWorld* World = RuntimeToolCall::GetWorld();
		Out.bOnScreen = World && World->GetGameViewport() != nullptr;
	}
	return Out;
}

void URuntimeLogicToolset::RemoveUi(const TArray<FString>& Names)
{
	EditLogicDefinitions(TEXT("remove ui"), [&](FLogicWorld& L, FString& Err)
	{
		for (const FString& N : Names)
		{
			if (L.Ui.RemoveAll([&N](const FLogicUiItem& U) { return U.Name == N; }) == 0)
			{
				TArray<FString> Known;
				for (const FLogicUiItem& U : L.Ui)
				{
					Known.Add(U.Name);
				}
				Err = FString::Printf(TEXT("no ui item '%s' (items: %s)"), *N, Known.Num() ? *FString::Join(Known, TEXT(", ")) : TEXT("none"));
				return false;
			}
		}
		L.RefreshUi();
		return true;
	});
}

FLogicInputModeResult URuntimeLogicToolset::SetInputMode(ELogicInputMode Mode, bool bLockCamera)
{
	FLogicInputModeResult Out;
	const FString ModeName = LogicEnumString(Mode);
	if (EditLogicDefinitions(TEXT("input mode"), [&](FLogicWorld& L, FString&)
	{
		L.InputMode = ModeName;
		L.bLockCamera = bLockCamera;
		L.ApplyInputMode();
		return true;
	}))
	{
		Out.Mode = ModeName;
		Out.bLockCamera = bLockCamera;
		const UWorld* World = RuntimeToolCall::GetWorld();
		Out.bHasPlayer = World && World->GetFirstPlayerController() != nullptr;
	}
	return Out;
}

// ---- Running --------------------------------------------------------------------------------------------------------

FLogicGameState URuntimeLogicToolset::Start()
{
	FLogicWorld& L = LogicFor();
	L.Start();
	return LogicState(L, 5);
}

void URuntimeLogicToolset::Stop()
{
	LogicFor().Stop();
}

FLogicFireReport URuntimeLogicToolset::FireEvent(ELogicEventKind On, const FRuntimeToolJson& Data, AActor* Target, const FString& Key, const FString& Name)
{
	FLogicFireReport Out;
	FLogicWorld& L = LogicFor();
	if (!LogicRequireRunning(L))
	{
		return Out;
	}
	const FString OnName = LogicEnumString(On);
	const TSharedPtr<FJsonObject> DataObj = Data.AsObject();
	if (Data.IsSet() && !DataObj.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("data must be an object"), TEXT("e.g. {\"from_above\": true}"));
		return Out;
	}
	FLogicEvent E;
	if (On == ELogicEventKind::Touch || On == ELogicEventKind::Click || On == ELogicEventKind::RightClick)
	{
		if (!Target)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("on %s needs target"), *OnName), TEXT("Pass the node, e.g. \"target\":\"cell_4\""));
			return Out;
		}
		const FRuntimeSceneNode* N = LogicNodeArg(Target, TEXT("target"));
		if (!N)
		{
			return Out;
		}
		bool bMade = false;
		if (On == ELogicEventKind::Touch)
		{
			bool bAbove = false, bBelow = false;
			if (DataObj.IsValid())
			{
				DataObj->TryGetBoolField(TEXT("from_above"), bAbove);
				DataObj->TryGetBoolField(TEXT("from_below"), bBelow);
			}
			bMade = L.MakeTouchEvent(N->Id, bAbove, bBelow, E);
		}
		else
		{
			bMade = L.MakeClickEventForNode(N->Id, On == ELogicEventKind::RightClick, E);
		}
		if (!bMade)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s target is not a scene node"), *OnName));
			return Out;
		}
	}
	else
	{
		const bool bNeedsKey = On == ELogicEventKind::Key;
		const bool bNeedsName = On == ELogicEventKind::Button || On == ELogicEventKind::Timer || On == ELogicEventKind::Event;
		if (bNeedsKey && Key.IsEmpty())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("on key needs key"), TEXT("e.g. \"key\":\"R\""));
			return Out;
		}
		if (bNeedsName && Name.IsEmpty())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("on %s needs name"), *OnName), TEXT("The button, timer or event name"));
			return Out;
		}
		E.On = OnName;
		E.Name = bNeedsKey ? Key : Name;
		if (Target)
		{
			if (const FRuntimeSceneNode* N = RuntimeToolCall::NodeOf(Target))
			{
				E.Target = N->Id;
			}
		}
		if (DataObj.IsValid())
		{
			E.Data = FLogicValue::FromJson(MakeShared<FJsonValueObject>(DataObj));
		}
	}
	Out = LogicStructFromJson<FLogicFireReport>(L.Fire(MoveTemp(E)));
	Out.Vars = LogicVarsJson(L.Vars);
	return Out;
}

FLogicGameState URuntimeLogicToolset::Step(float Seconds)
{
	FLogicWorld& L = LogicFor();
	if (!LogicRequireRunning(L))
	{
		return FLogicGameState();
	}
	if (Seconds < 0.f || Seconds > 600.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("seconds must be 0-600 (got %g)"), Seconds));
		return FLogicGameState();
	}
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Seconds / 0.1), 1, 6000);
	for (int32 i = 0; i < Steps; ++i)
	{
		L.Step(Seconds / Steps);
	}
	return LogicState(L, 10);
}

FLogicGameState URuntimeLogicToolset::GetState(int32 LogLimit)
{
	return LogicState(LogicFor(), FMath::Clamp(LogLimit, 0, 100));
}

FLogicExport URuntimeLogicToolset::Export()
{
	FLogicExport Out;
	Out.Logic = FRuntimeToolJson::Make(TSharedPtr<FJsonObject>(LogicFor().ExportDefinitions()));
	return Out;
}

FLogicImportResult URuntimeLogicToolset::Import(const FRuntimeToolJson& Logic)
{
	FLogicImportResult Out;
	const TSharedPtr<FJsonObject> Defs = Logic.AsObject();
	if (!Defs.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("logic must be an object"), TEXT("Pass the \"logic\" object returned by logic.export"));
		return Out;
	}
	if (EditLogicDefinitions(TEXT("import logic"), [&](FLogicWorld& L, FString& Err) { return L.ImportDefinitions(Defs, Err); }))
	{
		Out.Rules = LogicFor().Rules.Num();
		Out.Vars = LogicFor().InitialVars.Num();
	}
	return Out;
}

// ---- Player and motion ----------------------------------------------------------------------------------------------

FLogicPlayerState URuntimeLogicToolset::SpawnPlayer(const FVector& Location, const FLogicDash& Dash, const float& Speed, const float& MinSpeed,
	const float& MaxSpeed, const float& CameraDistance, const float& CameraHeight, const float& CameraLead, const float& CameraPitch, const float& Fov,
	const float& KillZ, const float& CrashDamage, const float& Grid, ELogicPlayerMode Mode, float Yaw, float Turn, float RollRate, float MouseSensitivity,
	const FString& Secondary, bool bAutoSpawn, float StepTime, const FString& ReloadKey, AActor* Body, float Radius, float Height, float Jump,
	float JumpHold, float Gravity, float AirControl)
{
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({});
	LogicNormalizeEnum(Def, TEXT("mode"), Mode);
	if (Body)
	{
		const FRuntimeSceneNode* N = LogicNodeArg(Body, TEXT("body"));
		if (!N)
		{
			return FLogicPlayerState();
		}
		Def->SetStringField(TEXT("body"), N->Id);
	}
	if (EditLogicDefinitions(TEXT("spawn player"), [&](FLogicWorld& L, FString& Err)
	{
		L.PlayerDef = Def;
		L.PlayerLive = nullptr;
		return bAutoSpawn ? L.SpawnPlayer(Err) : true;
	}))
	{
		return LogicPlayerState(LogicFor());
	}
	return FLogicPlayerState();
}

void URuntimeLogicToolset::RemovePlayer()
{
	EditLogicDefinitions(TEXT("remove player"), [](FLogicWorld& L, FString&)
	{
		L.PlayerDef = nullptr;
		L.RemovePlayer();
		return true;
	});
}

FLogicNodesResult URuntimeLogicToolset::SetMotion(const TArray<FString>& Targets, const FLogicMotion& Motion)
{
	FLogicNodesResult Out;
	if (!LogicRequireTargets(Targets))
	{
		return Out;
	}
	TSharedPtr<FJsonObject> MotionJson;
	const TSharedPtr<FJsonObject>* Raw = nullptr;
	const TSharedRef<FJsonObject> Passed = LogicPassedArgs({});
	if (Passed->TryGetObjectField(TEXT("motion"), Raw))
	{
		MotionJson = *Raw;
	}
	if (!MotionJson.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("motion must be an object"), TEXT("e.g. {\"type\":\"patrol\",\"distance\":500}"));
		return Out;
	}
	if (MotionJson->HasField(TEXT("type")))
	{
		MotionJson->SetStringField(TEXT("type"), LogicEnumString(Motion.Type));
	}
	TArray<FString> Ids;
	if (EditLogicDefinitions(TEXT("set motion"), [&](FLogicWorld& L, FString& Err)
	{
		if (!L.ResolveNodes(Targets, Ids, Err))
		{
			return false;
		}
		FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
		for (const FString& Id : Ids)
		{
			const FRuntimeSceneNode* N = Graph ? Graph->Find(Id) : nullptr;
			AActor* A = N ? N->Actor.Get() : nullptr;
			if (!A)
			{
				continue;
			}
			// Keep the original start pose when a motion is redefined.
			TSharedRef<FJsonObject> M = RuntimeJson::Parse(RuntimeJson::Write(MotionJson.ToSharedRef())).ToSharedRef();
			if (const TSharedPtr<FJsonObject>* Prev = L.Motions.Find(Id))
			{
				M->SetField(TEXT("origin"), (*Prev)->TryGetField(TEXT("origin")));
				M->SetField(TEXT("origin_rot"), (*Prev)->TryGetField(TEXT("origin_rot")));
			}
			else
			{
				M->SetField(TEXT("origin"), RuntimeJson::Vec(A->GetActorLocation()));
				M->SetField(TEXT("origin_rot"), RuntimeJson::Rot(A->GetActorRotation()));
			}
			L.Motions.Add(Id, M);
		}
		return true;
	}))
	{
		Out.Nodes = Ids.Num();
	}
	return Out;
}

void URuntimeLogicToolset::ClearMotion(const TArray<FString>& Targets)
{
	if (!LogicRequireTargets(Targets))
	{
		return;
	}
	EditLogicDefinitions(TEXT("clear motion"), [&](FLogicWorld& L, FString& Err)
	{
		TArray<FString> Ids;
		if (!L.ResolveNodes(Targets, Ids, Err))
		{
			return false;
		}
		for (const FString& Id : Ids)
		{
			L.Motions.Remove(Id);
		}
		return true;
	});
}

FLogicSolidResult URuntimeLogicToolset::SetSolid(const TArray<FString>& Targets, bool bSolid)
{
	FLogicSolidResult Out;
	if (!LogicRequireTargets(Targets))
	{
		return Out;
	}
	int32 Count = 0;
	if (EditLogicDefinitions(TEXT("set solid"), [&](FLogicWorld& L, FString& Err)
	{
		TArray<FString> Ids;
		if (!L.ResolveNodes(Targets, Ids, Err))
		{
			return false;
		}
		for (const FString& Id : Ids)
		{
			L.Solid.Add(Id, bSolid);
		}
		Count = Ids.Num();
		L.ApplySolid();
		return true;
	}))
	{
		Out.Nodes = Count;
		Out.bSolid = bSolid;
	}
	return Out;
}

// ---- Combat ---------------------------------------------------------------------------------------------------------

FLogicHealthResult URuntimeLogicToolset::DefineHealth(const TArray<FString>& Targets, const TMap<FString, float>& Resistances, const float& Initial,
	const float& Invulnerable, const ELogicDeathAction& OnDeath, float Max, const FString& PointsVar, float Points, const FString& HitFeedback,
	const FString& DeathFeedback, const FString& Loot)
{
	FLogicHealthResult Out;
	if (!LogicRequireTargets(Targets))
	{
		return Out;
	}
	if (Max <= 0.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("max must be > 0 (got %g)"), Max));
		return Out;
	}
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({TEXT("targets")});
	LogicNormalizeEnum(Def, TEXT("on_death"), OnDeath);
	if (EditLogicDefinitions(TEXT("define health"), [&](FLogicWorld& L, FString& Err)
	{
		for (const FString& T : Targets)
		{
			TArray<FString> Ids;
			if (T != TEXT("player") && !L.ResolveNodes({T}, Ids, Err))
			{
				return false;
			}
			L.HealthDefs.Add(T, Def);
		}
		L.RebuildHealths(false);
		return true;
	}))
	{
		Out.Health = LogicHealthMap(LogicFor());
	}
	return Out;
}

void URuntimeLogicToolset::DefineHitzone(const TArray<FString>& Targets, const float& DamageMax, float Damage, const FString& Type, const FString& Affects,
	ELogicHitZoneMode Mode, float Interval, float Knockback)
{
	if (!LogicRequireTargets(Targets))
	{
		return;
	}
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({TEXT("targets")});
	LogicNormalizeEnum(Def, TEXT("mode"), Mode);
	EditLogicDefinitions(TEXT("define hit zone"), [&](FLogicWorld& L, FString& Err)
	{
		for (const FString& T : Targets)
		{
			TArray<FString> Ids;
			if (!L.ResolveNodes({T}, Ids, Err))
			{
				return false;
			}
			L.HitZoneDefs.Add(T, Def);
		}
		return true;
	});
}

FLogicWeaponResult URuntimeLogicToolset::DefineWeapon(const FString& Name, const FLogicBulletLook& Bullet, ELogicWeaponMode Mode, float Damage,
	const FString& Type, float Rate, bool bAuto, int32 Burst, float BurstGap, int32 Magazine, float Reload, bool bAutoReload, const FString& AmmoVar,
	int32 PerShot, float Spread, ELogicSpreadMode SpreadMode, float Speed, float Lifetime, float Range, float Radius, float Knockback, float Muzzle,
	const FString& Hits, const FString& Pool, int32 PoolSize, const FString& Feedback, float Noise, float Homing, float LockRange, float LockAngle)
{
	FLogicWeaponResult Out;
	if (!LogicRequireName(Name, TEXT("weapon")))
	{
		return Out;
	}
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({TEXT("name"), TEXT("bullet")});
	LogicNormalizeEnum(Def, TEXT("mode"), Mode);
	LogicNormalizeEnum(Def, TEXT("spread_mode"), SpreadMode);
	const FToolInvocation* Inv = RuntimeToolCall::Current();
	FToolCallerContext Caller;
	Caller.SessionId = Inv ? Inv->GetCaller().SessionId : FString(TEXT("default"));
	const int32 Depth = Inv ? Inv->GetDepth() + 1 : 1;
	if (EditLogicDefinitions(TEXT("define weapon"), [&](FLogicWorld& L, FString& Err)
	{
		FString PoolName = Pool;
		if (PoolName.IsEmpty())
		{
			// A default glowing bullet in its own pool.
			const FVector Color(Bullet.Color.R, Bullet.Color.G, Bullet.Color.B);
			const double Size = Bullet.Size;
			const bool bHitscan = Mode == ELogicWeaponMode::Hitscan;
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("shape"), bHitscan ? TEXT("box") : TEXT("sphere"));
			Args->SetField(TEXT("size"), RuntimeJson::Vec(bHitscan ? FVector(Size * 3, Size * 0.4, Size * 0.4) : FVector(Size)));
			Args->SetField(TEXT("location"), RuntimeJson::Vec(FVector(0, 0, -100000)));
			Args->SetStringField(TEXT("name"), TEXT("bullet_") + Name);
			Args->SetStringField(TEXT("pivot"), TEXT("center"));
			Args->SetStringField(TEXT("material"), TEXT("light"));
			Args->SetField(TEXT("color"), RuntimeJson::Vec(Color));
			Args->SetField(TEXT("emissive"), RuntimeJson::Vec(Color));
			Args->SetNumberField(TEXT("emissive_strength"), 12.0);
			const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("build.create_primitive"), Args, Caller, Depth);
			FString TemplateId;
			if (!R.IsOk() || !R.Payload.IsValid() || !R.Payload->TryGetStringField(TEXT("id"), TemplateId))
			{
				Err = TEXT("could not create the bullet: ") + R.Message;
				return false;
			}
			PoolName = TEXT("bullets_") + Name;
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("template"), TemplateId);
			P->SetNumberField(TEXT("size"), FMath::Clamp(PoolSize, 1, 500));
			L.Pools.Add(PoolName, P);
			Def->SetStringField(TEXT("pool"), PoolName);
		}
		else if (!L.Pools.Contains(PoolName))
		{
			Err = FString::Printf(TEXT("no pool '%s' (pools: %s; create one with logic.define_pool)"), *PoolName, *LogicKeyList(L.Pools));
			return false;
		}
		L.WeaponDefs.Add(Name, Def);
		Out.Pool = PoolName;
		return L.EnsurePool(PoolName, Err);
	}))
	{
		Out.Weapon = Name;
	}
	return Out;
}

void URuntimeLogicToolset::Equip(const FString& Owner, const FString& Weapon)
{
	if (!LogicRequireName(Owner, TEXT("owner")))
	{
		return;
	}
	EditLogicDefinitions(TEXT("equip"), [&](FLogicWorld& L, FString& Err)
	{
		if (!Weapon.IsEmpty() && !L.WeaponDefs.Contains(Weapon))
		{
			Err = FString::Printf(TEXT("no weapon '%s' (weapons: %s; define it with logic.define_weapon)"), *Weapon, *LogicKeyList(L.WeaponDefs));
			return false;
		}
		TArray<FString> Ids;
		if (Owner != TEXT("player") && !L.ResolveNodes({Owner}, Ids, Err))
		{
			return false;
		}
		if (Weapon.IsEmpty())
		{
			L.EquipDefs.Remove(Owner);
		}
		else
		{
			L.EquipDefs.Add(Owner, Weapon);
		}
		return true;
	});
}

FLogicDamageResult URuntimeLogicToolset::Damage(const FString& Target, float Amount, const FString& Type)
{
	FLogicDamageResult Out;
	FLogicWorld& L = LogicFor();
	FString Victim = Target;
	if (Victim != TEXT("player"))
	{
		TArray<FString> Ids;
		FString Err;
		if (!L.ResolveNodes({Target}, Ids, Err) || Ids.Num() == 0)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, Err.IsEmpty() ? FString::Printf(TEXT("no node '%s'"), *Target) : Err,
				TEXT("Pass \"player\" or a node id, path or name"));
			return Out;
		}
		Victim = Ids[0];
	}
	Out.bApplied = L.ApplyDamage(Victim, Amount, Type, TEXT("tool"), FVector::ZeroVector);
	Out.Health = LogicHealthMap(L);
	return Out;
}

// ---- AI -------------------------------------------------------------------------------------------------------------

FLogicBrainResult URuntimeLogicToolset::DefineBrain(const FString& Name, const TArray<FRuntimeToolJson>& States, const FLogicBrainVision& Vision,
	const FLogicBrainHearing& Hearing, const FLogicBrainFlight& Flight, const float& TurnSpeed, const float& DecideEvery, const FString& Start,
	ELogicBrainBackend Backend)
{
	FLogicBrainResult Out;
	if (!LogicRequireName(Name, TEXT("brain")))
	{
		return Out;
	}
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({TEXT("name")});
	Def->SetStringField(TEXT("backend"), Backend == ELogicBrainBackend::Ai ? TEXT("ai") : TEXT("native"));
	if (EditLogicDefinitions(TEXT("define brain"), [&](FLogicWorld& L, FString& Err)
	{
		if (!FLogicWorld::ValidateBrain(Def, Err))
		{
			return false;
		}
		L.BrainDefs.Add(Name, Def);
		if (L.IsRunning())
		{
			L.ResetBrains();
		}
		return true;
	}))
	{
		Out.Brain = Name;
	}
	return Out;
}

FLogicAssignBrainResult URuntimeLogicToolset::AssignBrain(const TArray<FString>& Targets, const FString& Brain, const FLogicPatrolRoute& Patrol)
{
	FLogicAssignBrainResult Out;
	if (!LogicRequireTargets(Targets))
	{
		return Out;
	}
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({TEXT("targets")});
	const FToolInvocation* Inv = RuntimeToolCall::Current();
	const FString Session = Inv ? Inv->GetCaller().SessionId : FString(TEXT("default"));
	int32 Count = 0;
	if (EditLogicDefinitions(TEXT("assign brain"), [&](FLogicWorld& L, FString& Err)
	{
		const TSharedPtr<FJsonObject>* BrainDef = L.BrainDefs.Find(Brain);
		if (!BrainDef)
		{
			Err = FString::Printf(TEXT("no brain '%s' (brains: %s; define it with logic.define_brain)"), *Brain, *LogicKeyList(L.BrainDefs));
			return false;
		}
		for (const FString& T : Targets)
		{
			TArray<FString> Ids;
			if (!L.ResolveNodes({T}, Ids, Err))
			{
				return false;
			}
			for (const FString& Id : Ids)
			{
				if (!L.CreateBrainVisuals(Id, *BrainDef, Session, Err))
				{
					return false;
				}
			}
			Count += Ids.Num();
			L.BrainAssign.Add(T, Def);
		}
		if (L.IsRunning())
		{
			L.ResetBrains();
		}
		return true;
	}))
	{
		Out.Agents = Count;
	}
	return Out;
}

void URuntimeLogicToolset::SetHud(bool bEnemyBars, bool bAlwaysBars, bool bAlertIcons, bool bDamageNumbers, bool bHitFlash, bool bLowHealth, bool bMarkers)
{
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({});
	EditLogicDefinitions(TEXT("set hud"), [&](FLogicWorld& L, FString&)
	{
		L.HudDef = Def;
		L.RefreshUi();
		return true;
	});
}

// ---- Game feel ------------------------------------------------------------------------------------------------------

void URuntimeLogicToolset::DefineFeedback(const FString& Name, const TArray<FRuntimeToolJson>& Steps)
{
	if (!LogicRequireName(Name, TEXT("feedback")))
	{
		return;
	}
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({TEXT("name")});
	EditLogicDefinitions(TEXT("define feedback"), [&](FLogicWorld& L, FString& Err)
	{
		static const TArray<FString> Types = {TEXT("shake"), TEXT("flash"), TEXT("freeze"), TEXT("slowmo"), TEXT("text"), TEXT("tween"), TEXT("spawn"), TEXT("emit")};
		for (int32 i = 0; i < Steps.Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Step = Steps[i].AsObject();
			FString Type;
			if (!Step.IsValid() || !Step->TryGetStringField(TEXT("type"), Type) || !Types.Contains(Type))
			{
				Err = FString::Printf(TEXT("steps[%d] needs type: %s"), i, *FString::Join(Types, TEXT(", ")));
				return false;
			}
			if (Type == TEXT("spawn") && !L.Pools.Contains(Str(Step, TEXT("pool"))))
			{
				Err = FString::Printf(TEXT("steps[%d]: spawn needs an existing pool (pools: %s)"), i, *LogicKeyList(L.Pools));
				return false;
			}
		}
		L.FeedbackDefs.Add(Name, Def);
		return true;
	});
}

void URuntimeLogicToolset::PlayFeedback(const FString& Name, const FVector& At, const FString& Target)
{
	FLogicWorld& L = LogicFor();
	if (!L.FeedbackDefs.Contains(Name))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no feedback '%s' (presets: %s)"), *Name, *LogicKeyList(L.FeedbackDefs)),
			TEXT("Define it with logic.define_feedback"));
		return;
	}
	FVector Where = At;
	if (!LogicArgPassed(TEXT("at")))
	{
		Where = FVector::ZeroVector;
		L.OwnerLocation(TEXT("player"), Where);
	}
	L.PlayFeedback(Name, Where, Target);
}

FLogicTweenResult URuntimeLogicToolset::Tween(const TArray<FString>& Targets, const FVector& Location, const FVector& Move, const FRotator& Rotation,
	const FRotator& Rotate, const FRuntimeToolJson& Scale, const FLinearColor& Color, float Duration, float Delay, ELogicEase Ease, ELogicTweenLoop Loop,
	int32 Repeat, ELogicTweenEnd Then, const FString& Emit, bool bAdd)
{
	FLogicTweenResult Out;
	if (!LogicRequireTargets(Targets))
	{
		return Out;
	}
	FLogicWorld& L = LogicFor();
	TArray<FString> Ids;
	FString Err;
	if (!L.ResolveNodes(Targets, Ids, Err))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, Err, TEXT("Pass node ids, paths, names or \"tag:x\""));
		return Out;
	}
	const TSharedRef<FJsonObject> Spec = LogicPassedArgs({TEXT("targets")});
	LogicNormalizeEnum(Spec, TEXT("ease"), Ease);
	LogicNormalizeEnum(Spec, TEXT("loop"), Loop);
	LogicNormalizeEnum(Spec, TEXT("then"), Then);
	if (LogicArgPassed(TEXT("color")))
	{
		Spec->SetField(TEXT("color"), RuntimeJson::Vec(FVector(Color.R, Color.G, Color.B)));
	}
	for (const FString& Id : Ids)
	{
		if (!L.StartTween(Id, Spec, Err))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err);
			return Out;
		}
	}
	Out.Nodes = Ids.Num();
	Out.ActiveTweens = L.NumTweens();
	return Out;
}

// ---- Pools and spawning ---------------------------------------------------------------------------------------------

FLogicPoolResult URuntimeLogicToolset::DefinePool(const FString& Name, AActor* Template, int32 Size)
{
	FLogicPoolResult Out;
	if (!LogicRequireName(Name, TEXT("pool")))
	{
		return Out;
	}
	if (Size < 1 || Size > 500)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("size must be 1-500 (got %d)"), Size));
		return Out;
	}
	const FRuntimeSceneNode* N = LogicNodeArg(Template, TEXT("template"));
	if (!N)
	{
		return Out;
	}
	const FString TemplateId = N->Id;
	if (EditLogicDefinitions(TEXT("define pool"), [&](FLogicWorld& L, FString& Err)
	{
		TSharedRef<FJsonObject> Def = MakeShared<FJsonObject>();
		Def->SetStringField(TEXT("template"), TemplateId);
		Def->SetNumberField(TEXT("size"), Size);
		L.Pools.Add(Name, Def);
		return L.EnsurePool(Name, Err);
	}))
	{
		Out.Pool = Name;
		Out.Free = LogicFor().PoolFree(Name);
	}
	return Out;
}

void URuntimeLogicToolset::DefineSpawner(const FString& Name, const FString& Pool, const TArray<FVector>& Points, const FRuntimeToolJson& AroundPlayer,
	const FRuntimeToolJson& Interval, const TArray<FLogicSpawnerWave>& Waves, const int32& Total, int32 MaxAlive, float WaveGap, float StartDelay,
	bool bAutostart, bool bFacePlayer)
{
	if (!LogicRequireName(Name, TEXT("spawner")))
	{
		return;
	}
	if (Points.Num() == 0 && !AroundPlayer.AsObject().IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("spawner needs points or around_player"),
			TEXT("points:[[x,y,z],...] or around_player:{radius, height}"));
		return;
	}
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({TEXT("name")});
	EditLogicDefinitions(TEXT("define spawner"), [&](FLogicWorld& L, FString& Err)
	{
		if (!L.Pools.Contains(Pool))
		{
			Err = FString::Printf(TEXT("no pool '%s' (pools: %s; create one with logic.define_pool)"), *Pool, *LogicKeyList(L.Pools));
			return false;
		}
		L.SpawnerDefs.Add(Name, Def);
		return true;
	});
}

void URuntimeLogicToolset::DefineLoot(const FString& Name, const TArray<FLogicLootEntry>& Entries, const FRuntimeToolJson& Count, float Scatter)
{
	if (!LogicRequireName(Name, TEXT("loot table")))
	{
		return;
	}
	if (Entries.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("entries is empty"), TEXT("e.g. [{\"pool\":\"coins\",\"weight\":1}]"));
		return;
	}
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({TEXT("name")});
	EditLogicDefinitions(TEXT("define loot"), [&](FLogicWorld& L, FString& Err)
	{
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			const FString& P = Entries[i].Pool;
			if (P != TEXT("none") && !L.Pools.Contains(P))
			{
				Err = FString::Printf(TEXT("entries[%d]: no pool '%s' (pools: %s; or \"none\" for a no-drop chance)"), i, *P, *LogicKeyList(L.Pools));
				return false;
			}
		}
		L.LootDefs.Add(Name, Def);
		return true;
	});
}

void URuntimeLogicToolset::DefineInteractable(const TArray<FString>& Targets, const FString& Prompt, const FString& Key, float Radius, bool bOnce,
	float Cooldown, const FString& Requires, const FString& LockedPrompt)
{
	if (!LogicRequireTargets(Targets))
	{
		return;
	}
	if (!Requires.IsEmpty())
	{
		FString E;
		if (!LogicExpr::Parse(Requires, E))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("requires: ") + E, TEXT("A condition, e.g. has('red_key')"));
			return;
		}
	}
	const TSharedRef<FJsonObject> Def = LogicPassedArgs({TEXT("targets")});
	EditLogicDefinitions(TEXT("define interactable"), [&](FLogicWorld& L, FString& Err)
	{
		for (const FString& T : Targets)
		{
			TArray<FString> Ids;
			if (!L.ResolveNodes({T}, Ids, Err))
			{
				return false;
			}
			L.InteractDefs.Add(T, Def);
		}
		return true;
	});
}

// ---- Saves and tuning -----------------------------------------------------------------------------------------------

void URuntimeLogicToolset::Save(const FString& Slot, const TArray<FString>& Vars)
{
	FLogicWorld& L = LogicFor();
	for (const FString& V : Vars)
	{
		if (!L.Vars.Contains(V))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no variable '%s' (defined: %s)"), *V, *LogicKeyList(L.Vars)));
			return;
		}
	}
	FString Err;
	if (!L.SaveSlot(Slot, Vars, Err))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err);
	}
}

FLogicVarsResult URuntimeLogicToolset::Load(const FString& Slot)
{
	FLogicVarsResult Out;
	FLogicWorld& L = LogicFor();
	FString Err;
	if (!L.LoadSlot(Slot, Err))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, Err, TEXT("Save it first with logic.save or the {\"save\": slot} action"));
		return Out;
	}
	Out.Vars = LogicVarsJson(L.Vars);
	return Out;
}

FLogicTuning URuntimeLogicToolset::GetTuning()
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetObjectField(TEXT("tuning"), LogicTuning::ToJson());
	J->SetStringField(TEXT("note"), TEXT("Internal constants. Override in DefaultGame.ini [RuntimeToolsets.Tuning] name=value or with logic.set_tuning. ")
		TEXT("Gameplay parameters belong to define_* tools."));
	return LogicStructFromJson<FLogicTuning>(J);
}

FLogicTuning URuntimeLogicToolset::SetTuning(const TMap<FString, double>& Values, bool bReset)
{
	if (bReset)
	{
		LogicTuning::Reset();
	}
	for (const TPair<FString, double>& P : Values)
	{
		FString Err;
		if (!LogicTuning::Set(P.Key, P.Value, Err))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err, TEXT("See logic.get_tuning for the names"));
			return FLogicTuning();
		}
	}
	return GetTuning();
}
