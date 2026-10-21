#include "LogicToolset.h"
#include "LogicRuntime.h"
#include "RuntimeCommand.h"
#include "RuntimeObjectResolver.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolHelpers.h"
#include "RuntimeJson.h"

using namespace RuntimeTools;

namespace
{
	const TCHAR* Guide = TEXT(R"GUIDE(
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
PLATFORMERS / ACTION GAMES:
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
TOP-DOWN / SHOOTERS: logic.spawn_player {mode:"topdown", location, speed} -> WASD moves on the ground, the hero faces
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
TWEENS (animation): action {"tween":"event.target","move":[0,0,40],"duration":0.12,"ease":"out_quad","loop":"yoyo"}
   fields: location|move (relative), rotation|rotate (relative), scale (number or [x,y,z]), color [r,g,b], duration, delay,
   ease (linear in_quad out_quad in_out_quad in_cubic out_cubic in_out_cubic in_sine out_sine in_out_sine in_back out_back
   in_out_back out_elastic in_bounce out_bounce), loop (once|yoyo|loop|pingpong), repeat, then (hide|show|release),
   emit (event name when done), add (stack with other tweens). Values may be "=expr". Also the logic.tween tool (any time).
   Examples: stomp squash {"tween":"event.target","scale":[1.4,1.4,0.2],"duration":0.25,"ease":"out_back","then":"hide"};
   coin collect {"tween":"event.target","move":[0,0,150],"scale":0.2,"duration":0.3,"then":"hide"};
   block bump {"tween":"event.target","move":[0,0,30],"duration":0.08,"loop":"yoyo"}.
POOLS (reuse objects instead of creating them): logic.define_pool {name:"pop", template:"<node>", size:8} hides the
   template and pre-makes copies. Action {"spawn":"pop","at":"[x,y,z] expr","as":"p"} shows a free copy there (p.id);
   {"release":"p.id"} or tween then:"release" returns it. Restart returns all. Use for coins from blocks, bullets, effects.
Computer opponent: on var_changed turn (filter:"turn", condition:"turn == 'O'"), start a short timer, then pick
   winning_move(board,lines,'O','') or block winning_move(board,lines,'X','') or 4 or pick(indices(board,'')).
)GUIDE");

	FLogicWorld* Logic(FToolInvocation& Inv)
	{
		return &FLogicManager::Get().For(Inv.GetWorld());
	}

	/** Applies an edit to the definitions as one undoable step (undo restores the previous definitions). */
	bool EditDefinitions(FToolInvocation& Inv, const FString& Label, TFunctionRef<bool(FLogicWorld&, FString&)> Edit)
	{
		FLogicWorld& L = *Logic(Inv);
		const TSharedRef<FJsonObject> Before = L.ExportDefinitions();
		FString Err;
		if (!Edit(L, Err))
		{
			FString Ignore;
			L.ImportDefinitions(Before, Ignore);
			Inv.Fail(ERuntimeToolError::InvalidArguments, Err, TEXT("See logic.get_guide for the rule format"));
			return false;
		}
		const TSharedRef<FJsonObject> After = L.ExportDefinitions();
		TWeakObjectPtr<UWorld> W = Inv.GetWorld();
		auto Apply = [W](const TSharedRef<FJsonObject>& Defs)
		{
			FLogicWorld* Lw = W.IsValid() ? FLogicManager::Get().Find(W.Get()) : nullptr;
			FString E;
			return Lw && Lw->ImportDefinitions(Defs, E);
		};
		return Inv.RunCommand(MakeUnique<FLambdaCommand>(Label,
			[Apply, After](FRuntimeCommandContext&, FString&) { return Apply(After); },
			[Apply, Before](FRuntimeCommandContext&) { return Apply(Before); }));
	}

	TSharedRef<FJsonObject> VarsJson(const TMap<FString, FLogicValue>& Vars)
	{
		TSharedRef<FJsonObject> J = Obj();
		for (const TPair<FString, FLogicValue>& P : Vars)
		{
			J->SetField(P.Key, P.Value.ToJson());
		}
		return J;
	}
}

static void GetGuide(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("guide"), FString(Guide).TrimStartAndEnd());
	Inv.Succeed(Out);
}

static void DefineVars(FToolInvocation& Inv)
{
	const TSharedPtr<FJsonObject> Vars = Inv.GetObject(TEXT("vars"));
	const bool bReplace = Inv.GetBool(TEXT("replace"));
	if (EditDefinitions(Inv, TEXT("define vars"), [&](FLogicWorld& L, FString& Err)
	{
		if (bReplace)
		{
			L.InitialVars.Reset();
			L.Vars.Reset();
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Vars->Values)
		{
			if (P.Key.IsEmpty() || !FChar::IsAlpha(P.Key[0]) || P.Key == TEXT("event") || P.Key == TEXT("time"))
			{
				Err = FString::Printf(TEXT("bad variable name '%s' (letters, digits, _; not 'event' or 'time')"), *P.Key);
				return false;
			}
			const FLogicValue V = FLogicValue::FromJson(P.Value);
			L.InitialVars.Add(P.Key, V);
			L.Vars.Add(P.Key, V.DeepCopy());
		}
		return true;
	}))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetObjectField(TEXT("vars"), VarsJson(Logic(Inv)->InitialVars));
		Inv.Succeed(Out);
	}
}

static void SetVar(FToolInvocation& Inv)
{
	FLogicWorld& L = *Logic(Inv);
	const FString Name = Inv.GetString(TEXT("name"));
	if (!L.Vars.Contains(Name))
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no variable '%s'"), *Name), TEXT("Define it with logic.define_vars"));
		return;
	}
	FLogicValue V;
	if (Inv.Has(TEXT("expr")))
	{
		FString Err;
		V = L.Evaluate(Inv.GetString(TEXT("expr")), Err);
		if (!Err.IsEmpty())
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, Err);
			return;
		}
	}
	else
	{
		V = FLogicValue::FromJson(Inv.GetValue(TEXT("value")));
	}
	L.SetVar(Name, V, true);
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetField(TEXT("value"), L.Vars.FindRef(Name).ToJson());
	Inv.Succeed(Out);
}

static void Eval(FToolInvocation& Inv)
{
	FString Err;
	const FLogicValue V = Logic(Inv)->Evaluate(Inv.GetString(TEXT("expr")), Err);
	if (!Err.IsEmpty())
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, Err);
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetField(TEXT("value"), V.ToJson());
	Out->SetStringField(TEXT("type"), V.TypeName());
	Inv.Succeed(Out);
}

static void DefineRules(FToolInvocation& Inv)
{
	const TSharedPtr<FJsonValue> Raw = Inv.GetValue(TEXT("rules"));
	TArray<FString> Names;
	if (EditDefinitions(Inv, TEXT("define rules"), [&](FLogicWorld& L, FString& Err)
	{
		for (const TSharedPtr<FJsonValue>& Item : Raw->AsArray())
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			FLogicRule Rule;
			if (!Item->TryGetObject(O) || !FLogicRule::FromJson(*O, Rule, Err))
			{
				Err = Err.IsEmpty() ? TEXT("each rule must be an object") : Err;
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
			Names.Add(Rule.Name);
		}
		return true;
	}))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetArrayField(TEXT("defined"), [&Names]()
		{
			TArray<TSharedPtr<FJsonValue>> A;
			for (const FString& N : Names) A.Add(MakeShared<FJsonValueString>(N));
			return A;
		}());
		Out->SetNumberField(TEXT("rules"), Logic(Inv)->Rules.Num());
		Inv.Succeed(Out);
	}
}

static void RemoveRules(FToolInvocation& Inv)
{
	const TArray<FString> Names = Inv.GetStringArray(TEXT("names"));
	if (EditDefinitions(Inv, TEXT("remove rules"), [&](FLogicWorld& L, FString& Err)
	{
		for (const FString& N : Names)
		{
			if (L.Rules.RemoveAll([&N](const FLogicRule& R) { return R.Name == N; }) == 0)
			{
				Err = FString::Printf(TEXT("no rule '%s'"), *N);
				return false;
			}
		}
		return true;
	}))
	{
		Inv.Succeed();
	}
}

static void ListRules(FToolInvocation& Inv)
{
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FLogicRule& R : Logic(Inv)->Rules)
	{
		Items.Add(MakeShared<FJsonValueObject>(R.ToJson()));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetArrayField(TEXT("rules"), Items);
	Inv.Succeed(Out);
}

static void SetData(FToolInvocation& Inv)
{
	FResolvedTarget T;
	if (!Inv.ResolveTarget(TEXT("target"), T))
	{
		return;
	}
	if (!T.Node)
	{
		Inv.Fail(ERuntimeToolError::NotFound, TEXT("target is not a scene node"), TEXT("Use an id from build/scene/actors tools"));
		return;
	}
	const FString Id = T.Node->Id;
	const TSharedPtr<FJsonObject> Data = Inv.GetObject(TEXT("data"));
	if (EditDefinitions(Inv, TEXT("set node data"), [&](FLogicWorld& L, FString&)
	{
		L.NodeData.Add(Id, FLogicValue::FromJson(MakeShared<FJsonValueObject>(Data)));
		return true;
	}))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetStringField(TEXT("id"), Id);
		Inv.Succeed(Out);
	}
}

static void DefineUi(FToolInvocation& Inv)
{
	const TSharedPtr<FJsonValue> Raw = Inv.GetValue(TEXT("items"));
	if (EditDefinitions(Inv, TEXT("define ui"), [&](FLogicWorld& L, FString& Err)
	{
		for (const TSharedPtr<FJsonValue>& Item : Raw->AsArray())
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			FLogicUiItem U;
			if (!Item->TryGetObject(O) || !FLogicUiItem::FromJson(*O, U, Err))
			{
				Err = Err.IsEmpty() ? TEXT("each item must be an object") : Err;
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
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("items"), Logic(Inv)->Ui.Num());
		Out->SetBoolField(TEXT("on_screen"), Inv.GetWorld()->GetGameViewport() != nullptr);
		Inv.Succeed(Out);
	}
}

static void RemoveUi(FToolInvocation& Inv)
{
	const TArray<FString> Names = Inv.GetStringArray(TEXT("names"));
	if (EditDefinitions(Inv, TEXT("remove ui"), [&](FLogicWorld& L, FString& Err)
	{
		for (const FString& N : Names)
		{
			if (L.Ui.RemoveAll([&N](const FLogicUiItem& U) { return U.Name == N; }) == 0)
			{
				Err = FString::Printf(TEXT("no ui item '%s'"), *N);
				return false;
			}
		}
		L.RefreshUi();
		return true;
	}))
	{
		Inv.Succeed();
	}
}

static void SetInputMode(FToolInvocation& Inv)
{
	const FString Mode = Inv.GetString(TEXT("mode"), TEXT("cursor"));
	const bool bLock = Inv.GetBool(TEXT("lock_camera"));
	if (EditDefinitions(Inv, TEXT("input mode"), [&](FLogicWorld& L, FString&)
	{
		L.InputMode = Mode;
		L.bLockCamera = bLock;
		L.ApplyInputMode();
		return true;
	}))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetStringField(TEXT("mode"), Mode);
		Out->SetBoolField(TEXT("lock_camera"), bLock);
		Out->SetBoolField(TEXT("has_player"), Inv.GetWorld()->GetFirstPlayerController() != nullptr);
		Inv.Succeed(Out);
	}
}

static void Start(FToolInvocation& Inv)
{
	FLogicWorld& L = *Logic(Inv);
	L.Start();
	TSharedRef<FJsonObject> Out = L.GetState(5);
	Inv.Succeed(Out);
}

static void Stop(FToolInvocation& Inv)
{
	Logic(Inv)->Stop();
	Inv.Succeed();
}

static void FireEvent(FToolInvocation& Inv)
{
	FLogicWorld& L = *Logic(Inv);
	if (!L.IsRunning())
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("logic is not running"), TEXT("Call logic.start first"));
		return;
	}
	const FString On = Inv.GetString(TEXT("on"));
	FLogicEvent E;
	if (On == TEXT("touch"))
	{
		FResolvedTarget T;
		if (!Inv.ResolveTarget(TEXT("target"), T))
		{
			return;
		}
		const TSharedPtr<FJsonObject> D = Inv.GetObject(TEXT("data"));
		bool bAbove = false, bBelow = false;
		if (D.IsValid())
		{
			D->TryGetBoolField(TEXT("from_above"), bAbove);
			D->TryGetBoolField(TEXT("from_below"), bBelow);
		}
		if (!T.Node || !L.MakeTouchEvent(T.Node->Id, bAbove, bBelow, E))
		{
			Inv.Fail(ERuntimeToolError::NotFound, TEXT("touch target is not a scene node"));
			return;
		}
	}
	else if (On == TEXT("click") || On == TEXT("right_click"))
	{
		FResolvedTarget T;
		if (!Inv.ResolveTarget(TEXT("target"), T))
		{
			return;
		}
		if (!T.Node || !L.MakeClickEventForNode(T.Node->Id, On == TEXT("right_click"), E))
		{
			Inv.Fail(ERuntimeToolError::NotFound, TEXT("click target is not a scene node"));
			return;
		}
	}
	else
	{
		E.On = On;
		E.Name = Inv.GetString(On == TEXT("key") ? TEXT("key") : TEXT("name"));
		if (const TSharedPtr<FJsonObject> Data = Inv.GetObject(TEXT("data")))
		{
			E.Data = FLogicValue::FromJson(MakeShared<FJsonValueObject>(Data));
		}
	}
	TSharedRef<FJsonObject> Report = L.Fire(MoveTemp(E));
	Report->SetObjectField(TEXT("vars"), VarsJson(L.Vars));
	Inv.Succeed(Report);
}

static void Step(FToolInvocation& Inv)
{
	FLogicWorld& L = *Logic(Inv);
	if (!L.IsRunning())
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("logic is not running"), TEXT("Call logic.start first"));
		return;
	}
	const double Seconds = Inv.GetNumber(TEXT("seconds"), 1.0);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Seconds / 0.1), 1, 6000);
	for (int32 i = 0; i < Steps; ++i)
	{
		L.Step(Seconds / Steps);
	}
	Inv.Succeed(L.GetState(10));
}

static void GetState(FToolInvocation& Inv)
{
	Inv.Succeed(Logic(Inv)->GetState(static_cast<int32>(Inv.GetInt(TEXT("log_limit"), 20))));
}

static void Export(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetObjectField(TEXT("logic"), Logic(Inv)->ExportDefinitions());
	Inv.Succeed(Out);
}

static void Import(FToolInvocation& Inv)
{
	const TSharedPtr<FJsonObject> Defs = Inv.GetObject(TEXT("logic"));
	if (EditDefinitions(Inv, TEXT("import logic"), [&](FLogicWorld& L, FString& Err) { return L.ImportDefinitions(Defs, Err); }))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("rules"), Logic(Inv)->Rules.Num());
		Out->SetNumberField(TEXT("vars"), Logic(Inv)->InitialVars.Num());
		Inv.Succeed(Out);
	}
}


static TArray<FString> TargetsArg(FToolInvocation& Inv)
{
	TArray<FString> T = Inv.GetStringArray(TEXT("targets"));
	return T;
}

static TSharedRef<FJsonObject> ArgsWithout(FToolInvocation& Inv, std::initializer_list<const TCHAR*> Drop)
{
	TSharedRef<FJsonObject> Def = RuntimeJson::Parse(RuntimeJson::Write(Inv.GetArgs())).ToSharedRef();
	for (const TCHAR* F : Drop)
	{
		Def->RemoveField(F);
	}
	return Def;
}

static void DefineHealth(FToolInvocation& Inv)
{
	const TArray<FString> Targets = TargetsArg(Inv);
	const TSharedRef<FJsonObject> Def = ArgsWithout(Inv, {TEXT("targets")});
	if (EditDefinitions(Inv, TEXT("define health"), [&](FLogicWorld& L, FString& Err)
	{
		for (const FString& T : Targets)
		{
			if (T != TEXT("player"))
			{
				TArray<FString> Ids;
				if (!L.ResolveNodes({T}, Ids, Err))
				{
					return false;
				}
			}
			L.HealthDefs.Add(T, Def);
		}
		L.RebuildHealths(false);
		return true;
	}))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetField(TEXT("health"), Logic(Inv)->HealthMap().ToJson());
		Inv.Succeed(Out);
	}
}

static void DefineHitZone(FToolInvocation& Inv)
{
	const TArray<FString> Targets = TargetsArg(Inv);
	const TSharedRef<FJsonObject> Def = ArgsWithout(Inv, {TEXT("targets")});
	if (EditDefinitions(Inv, TEXT("define hit zone"), [&](FLogicWorld& L, FString& Err)
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
	}))
	{
		Inv.Succeed();
	}
}

static void DefineWeapon(FToolInvocation& Inv)
{
	const FString Name = Inv.GetString(TEXT("name"));
	TSharedRef<FJsonObject> Def = ArgsWithout(Inv, {TEXT("name"), TEXT("bullet")});
	const TSharedPtr<FJsonObject> Bullet = Inv.GetObject(TEXT("bullet"));
	if (EditDefinitions(Inv, TEXT("define weapon"), [&](FLogicWorld& L, FString& Err)
	{
		FString Pool;
		if (!Def->TryGetStringField(TEXT("pool"), Pool) || Pool.IsEmpty())
		{
			// A default glowing bullet in its own pool.
			const double Size = Bullet.IsValid() && Bullet->HasField(TEXT("size")) ? Bullet->GetNumberField(TEXT("size")) : 24.0;
			FVector Color(1.0, 0.8, 0.2);
			if (Bullet.IsValid())
			{
				RuntimeJson::ToVector(Bullet->TryGetField(TEXT("color")), Color);
			}
			TSharedRef<FJsonObject> Args = Obj();
			const bool bHitscan = Def->HasField(TEXT("mode")) && Def->GetStringField(TEXT("mode")) == TEXT("hitscan");
			Args->SetStringField(TEXT("shape"), bHitscan ? TEXT("box") : TEXT("sphere"));
			Args->SetField(TEXT("size"), RuntimeJson::Vec(bHitscan ? FVector(Size * 3, Size * 0.4, Size * 0.4) : FVector(Size)));
			Args->SetField(TEXT("location"), RuntimeJson::Vec(FVector(0, 0, -100000)));
			Args->SetStringField(TEXT("name"), TEXT("bullet_") + Name);
			Args->SetStringField(TEXT("pivot"), TEXT("center"));
			Args->SetStringField(TEXT("material"), TEXT("light"));
			Args->SetField(TEXT("color"), RuntimeJson::Vec(Color));
			Args->SetField(TEXT("emissive"), RuntimeJson::Vec(Color));
			Args->SetNumberField(TEXT("emissive_strength"), 12.0);
			FToolCallerContext Caller;
			Caller.SessionId = Inv.GetCaller().SessionId;
			const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("build.create_primitive"), Args, Caller, Inv.GetDepth() + 1);
			FString TemplateId;
			if (!R.IsOk() || !R.Payload->TryGetStringField(TEXT("id"), TemplateId))
			{
				Err = TEXT("could not create the bullet: ") + R.Message;
				return false;
			}
			Pool = TEXT("bullets_") + Name;
			TSharedRef<FJsonObject> P = Obj();
			P->SetStringField(TEXT("template"), TemplateId);
			P->SetNumberField(TEXT("size"), Def->HasField(TEXT("pool_size")) ? Def->GetNumberField(TEXT("pool_size")) : 40.0);
			L.Pools.Add(Pool, P);
			Def->SetStringField(TEXT("pool"), Pool);
		}
		else if (!L.Pools.Contains(Pool))
		{
			Err = FString::Printf(TEXT("no pool '%s' (logic.define_pool)"), *Pool);
			return false;
		}
		L.WeaponDefs.Add(Name, Def);
		return L.EnsurePool(Pool, Err);
	}))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetStringField(TEXT("weapon"), Name);
		Out->SetStringField(TEXT("pool"), Logic(Inv)->WeaponDefs[Name]->GetStringField(TEXT("pool")));
		Inv.Succeed(Out);
	}
}

static void Equip(FToolInvocation& Inv)
{
	const FString Owner = Inv.GetString(TEXT("owner"));
	const FString Weapon = Inv.GetString(TEXT("weapon"));
	if (EditDefinitions(Inv, TEXT("equip"), [&](FLogicWorld& L, FString& Err)
	{
		if (!Weapon.IsEmpty() && !L.WeaponDefs.Contains(Weapon))
		{
			Err = FString::Printf(TEXT("no weapon '%s' (logic.define_weapon)"), *Weapon);
			return false;
		}
		if (Owner != TEXT("player"))
		{
			TArray<FString> Ids;
			if (!L.ResolveNodes({Owner}, Ids, Err))
			{
				return false;
			}
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
	}))
	{
		Inv.Succeed();
	}
}

static void DamageTool(FToolInvocation& Inv)
{
	FLogicWorld& L = *Logic(Inv);
	FString Target = Inv.GetString(TEXT("target"));
	if (Target != TEXT("player"))
	{
		TArray<FString> Ids;
		FString Err;
		if (!L.ResolveNodes({Target}, Ids, Err))
		{
			Inv.Fail(ERuntimeToolError::NotFound, Err);
			return;
		}
		Target = Ids[0];
	}
	const bool bApplied = L.ApplyDamage(Target, Inv.GetNumber(TEXT("amount"), 1.0), Inv.GetString(TEXT("type")), TEXT("tool"), FVector::ZeroVector);
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetBoolField(TEXT("applied"), bApplied);
	Out->SetField(TEXT("health"), L.HealthMap().ToJson());
	Inv.Succeed(Out);
}

static TSharedPtr<FJsonObject> PlayerDefFromArgs(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Def = Obj();
	for (const TCHAR* F : {TEXT("location"), TEXT("radius"), TEXT("height"), TEXT("speed"), TEXT("jump"), TEXT("jump_hold"), TEXT("gravity"),
			 TEXT("air_control"), TEXT("body"), TEXT("camera_distance"), TEXT("camera_height"), TEXT("camera_lead"), TEXT("camera_pitch"), TEXT("fov"),
			 TEXT("kill_z"), TEXT("mode"), TEXT("reload_key")})
	{
		if (Inv.Has(F))
		{
			Def->SetField(F, Inv.GetValue(F));
		}
	}
	return Def;
}

static void TweenTool(FToolInvocation& Inv)
{
	FLogicWorld& L = *Logic(Inv);
	TArray<FString> Ids;
	FString Err;
	if (!L.ResolveNodes(Inv.GetStringArray(TEXT("targets")), Ids, Err))
	{
		Inv.Fail(ERuntimeToolError::NotFound, Err);
		return;
	}
	TSharedRef<FJsonObject> Spec = RuntimeJson::Parse(RuntimeJson::Write(Inv.GetArgs())).ToSharedRef();
	Spec->RemoveField(TEXT("targets"));
	for (const FString& Id : Ids)
	{
		if (!L.StartTween(Id, Spec, Err))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, Err);
			return;
		}
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("nodes"), Ids.Num());
	Out->SetNumberField(TEXT("active_tweens"), L.NumTweens());
	Inv.Succeed(Out);
}

static void DefinePool(FToolInvocation& Inv)
{
	FResolvedTarget T;
	if (!Inv.ResolveTarget(TEXT("template"), T))
	{
		return;
	}
	if (!T.Node)
	{
		Inv.Fail(ERuntimeToolError::NotFound, TEXT("template is not a scene node"));
		return;
	}
	const FString Name = Inv.GetString(TEXT("name"));
	const FString TemplateId = T.Node->Id;
	const int32 Size = static_cast<int32>(Inv.GetInt(TEXT("size"), 8));
	if (EditDefinitions(Inv, TEXT("define pool"), [&](FLogicWorld& L, FString& Err)
	{
		TSharedRef<FJsonObject> Def = Obj();
		Def->SetStringField(TEXT("template"), TemplateId);
		Def->SetNumberField(TEXT("size"), Size);
		L.Pools.Add(Name, Def);
		return L.EnsurePool(Name, Err);
	}))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetStringField(TEXT("pool"), Name);
		Out->SetNumberField(TEXT("free"), Logic(Inv)->PoolFree(Name));
		Inv.Succeed(Out);
	}
}

static void SpawnPlayerTool(FToolInvocation& Inv)
{
	const TSharedPtr<FJsonObject> Def = PlayerDefFromArgs(Inv);
	if (EditDefinitions(Inv, TEXT("spawn player"), [&](FLogicWorld& L, FString& Err)
	{
		L.PlayerDef = Def;
		return L.SpawnPlayer(Err);
	}))
	{
		TSharedRef<FJsonObject> Out = Logic(Inv)->PlayerJson();
		Out->SetStringField(TEXT("controls"), TEXT("A/D or arrows: run, Shift: sprint, Space/W/Up: jump (hold = higher)"));
		Inv.Succeed(Out);
	}
}

static void RemovePlayerTool(FToolInvocation& Inv)
{
	if (EditDefinitions(Inv, TEXT("remove player"), [&](FLogicWorld& L, FString&)
	{
		L.PlayerDef = nullptr;
		L.RemovePlayer();
		return true;
	}))
	{
		Inv.Succeed();
	}
}

static void SetMotion(FToolInvocation& Inv)
{
	const TArray<FString> Targets = Inv.GetStringArray(TEXT("targets"));
	const TSharedPtr<FJsonObject> Motion = Inv.GetObject(TEXT("motion"));
	TArray<FString> Ids;
	if (EditDefinitions(Inv, TEXT("set motion"), [&](FLogicWorld& L, FString& Err)
	{
		FString Type = TEXT("none");
		Motion->TryGetStringField(TEXT("type"), Type);
		if (Type != TEXT("patrol") && Type != TEXT("path") && Type != TEXT("bob") && Type != TEXT("none"))
		{
			Err = TEXT("motion type must be patrol, path, bob or none (spin works with any)");
			return false;
		}
		if (!L.ResolveNodes(Targets, Ids, Err))
		{
			return false;
		}
		FRuntimeSceneGraph* Graph = Inv.GetGraph();
		for (const FString& Id : Ids)
		{
			const FRuntimeSceneNode* N = Graph ? Graph->Find(Id) : nullptr;
			AActor* A = N ? N->Actor.Get() : nullptr;
			if (!A)
			{
				continue;
			}
			// Keep the original start pose when a motion is redefined.
			TSharedRef<FJsonObject> M = RuntimeJson::Parse(RuntimeJson::Write(Motion.ToSharedRef())).ToSharedRef();
			const TSharedPtr<FJsonObject>* Old = nullptr;
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
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("nodes"), Ids.Num());
		Inv.Succeed(Out);
	}
}

static void ClearMotion(FToolInvocation& Inv)
{
	const TArray<FString> Targets = Inv.GetStringArray(TEXT("targets"));
	if (EditDefinitions(Inv, TEXT("clear motion"), [&](FLogicWorld& L, FString& Err)
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
	}))
	{
		Inv.Succeed();
	}
}

static void SetSolid(FToolInvocation& Inv)
{
	const TArray<FString> Targets = Inv.GetStringArray(TEXT("targets"));
	const bool bSolid = Inv.GetBool(TEXT("solid"));
	int32 Count = 0;
	if (EditDefinitions(Inv, TEXT("set solid"), [&](FLogicWorld& L, FString& Err)
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
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("nodes"), Count);
		Out->SetBoolField(TEXT("solid"), bSolid);
		Inv.Succeed(Out);
	}
}

void FLogicToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("tween"), TEXT("Animate nodes smoothly: move/rotate/scale/color with easing, delay, yoyo/loop, and hide/show/release at the end."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), FToolParam::StringArray(TEXT("Node ids/paths/names or tag:x"), 500).Required())
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("Absolute end location")))
			.Add(TEXT("move"), FToolParam::Vec3(TEXT("Relative offset")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("Absolute end rotation")))
			.Add(TEXT("rotate"), FToolParam::Rotator(TEXT("Relative rotation (can exceed 360)")))
			.Add(TEXT("scale"), FToolParam::Any(TEXT("End scale: number or [x,y,z]")))
			.Add(TEXT("color"), FToolParam::Vec3(TEXT("End base color [r,g,b] (builder materials)")))
			.Add(TEXT("duration"), FToolParam::Number(TEXT("Seconds per cycle"), 0.001, 600.0).Default(0.3))
			.Add(TEXT("delay"), FToolParam::Number(TEXT("Seconds before starting"), 0.0, 600.0))
			.Add(TEXT("ease"), FToolParam::Enum(TEXT("Easing curve"), FLogicWorld::EaseNames()).Default(TEXT("out_quad")))
			.Add(TEXT("loop"), FToolParam::Enum(TEXT("once, yoyo (there and back), loop (restart), pingpong (forever)"), {TEXT("once"), TEXT("yoyo"), TEXT("loop"), TEXT("pingpong")}).Default(TEXT("once")))
			.Add(TEXT("repeat"), FToolParam::Number(TEXT("Cycles for yoyo/loop (-1 = forever for loop)"), -1.0, 10000.0))
			.Add(TEXT("then"), FToolParam::Enum(TEXT("At the end"), {TEXT("none"), TEXT("hide"), TEXT("show"), TEXT("release")}))
			.Add(TEXT("emit"), FToolParam::String(TEXT("Custom event fired at the end (rules on:event)"), 128))
			.Add(TEXT("add"), FToolParam::Boolean(TEXT("Run alongside other tweens on the node instead of replacing them"))))
		.RequiresWorld()
		.Handle(&TweenTool);

	B.Tool(TEXT("define_pool"), TEXT("Actor pool: pre-made hidden copies of a template node that rules spawn/release instead of creating objects."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Pool name"), 64).Required())
			.Add(TEXT("template"), FToolParam::Target(TEXT("Node to copy (it gets hidden)")).Required())
			.Add(TEXT("size"), FToolParam::Integer(TEXT("Copies"), 1, 500).Default(8)))
		.Undoable().RequiresWorld()
		.Handle(&DefinePool);

	B.Tool(TEXT("define_health"), TEXT("Give 'player' or nodes health: max, invulnerability after hits, resistances by damage type, what happens on death."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), FToolParam::StringArray(TEXT("'player', node ids/paths/names or tag:x"), 100).Required())
			.Add(TEXT("max"), FToolParam::Number(TEXT("Max health (default 10)"), 0.001))
			.Add(TEXT("initial"), FToolParam::Number(TEXT("Starting health (default max)"), 0.0))
			.Add(TEXT("invulnerable"), FToolParam::Number(TEXT("Seconds immune after a hit (player default 1)"), 0.0, 60.0))
			.Add(TEXT("resistances"), FToolParam::Object(TEXT("{damage type: multiplier}, e.g. {fire: 0.5, explosion: 2}")))
			.Add(TEXT("on_death"), FToolParam::Enum(TEXT("What happens at 0"), {TEXT("hide"), TEXT("squash"), TEXT("release"), TEXT("respawn"), TEXT("freeze"), TEXT("none")}))
			.Add(TEXT("points_var"), FToolParam::String(TEXT("Variable that gets 'points' on death"), 64))
			.Add(TEXT("points"), FToolParam::Number(TEXT("Points on death"))))
		.Undoable().RequiresWorld()
		.Handle(&DefineHealth);

	B.Tool(TEXT("define_hitzone"), TEXT("Nodes that damage what overlaps them (spikes, lava, explosions, melee areas): on enter or every interval while inside."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), FToolParam::StringArray(TEXT("Zone nodes: ids/paths/names or tag:x"), 100).Required())
			.Add(TEXT("damage"), FToolParam::Number(TEXT("Damage (default 1)")))
			.Add(TEXT("damage_max"), FToolParam::Number(TEXT("Random damage up to this")))
			.Add(TEXT("type"), FToolParam::String(TEXT("Damage type (for resistances)"), 32))
			.Add(TEXT("affects"), FToolParam::String(TEXT("'player', 'any' or tag:x (default any)"), 128))
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("enter: once per entry; stay: every interval"), {TEXT("enter"), TEXT("stay")}))
			.Add(TEXT("interval"), FToolParam::Number(TEXT("Seconds between hits in stay mode"), 0.05, 60.0))
			.Add(TEXT("knockback"), FToolParam::Number(TEXT("Push the player away (cm/s)"), 0.0)))
		.Undoable().RequiresWorld()
		.Handle(&DefineHitZone);

	B.Tool(TEXT("define_weapon"), TEXT("A projectile or hitscan weapon: damage, fire rate, burst, magazine/reload/ammo, shots per trigger, spread, bullets from a pool."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Weapon name"), 64).Required())
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("projectile or hitscan"), {TEXT("projectile"), TEXT("hitscan")}))
			.Add(TEXT("damage"), FToolParam::Number(TEXT("Per hit (default 1)")))
			.Add(TEXT("type"), FToolParam::String(TEXT("Damage type"), 32))
			.Add(TEXT("rate"), FToolParam::Number(TEXT("Shots per second (default 4)"), 0.01, 100.0))
			.Add(TEXT("auto"), FToolParam::Boolean(TEXT("Hold to keep firing (player)")))
			.Add(TEXT("burst"), FToolParam::Integer(TEXT("Shots per trigger pull"), 1, 50))
			.Add(TEXT("burst_gap"), FToolParam::Number(TEXT("Seconds between burst shots"), 0.0, 5.0))
			.Add(TEXT("magazine"), FToolParam::Integer(TEXT("Rounds per magazine (0 = unlimited)"), 0, 10000))
			.Add(TEXT("reload"), FToolParam::Number(TEXT("Reload seconds"), 0.0, 60.0))
			.Add(TEXT("auto_reload"), FToolParam::Boolean(TEXT("Reload when empty (default true)")))
			.Add(TEXT("ammo_var"), FToolParam::String(TEXT("Variable holding reserve ammo"), 64))
			.Add(TEXT("per_shot"), FToolParam::Integer(TEXT("Projectiles per shot (shotgun)"), 1, 64))
			.Add(TEXT("spread"), FToolParam::Number(TEXT("Degrees either side"), 0.0, 180.0))
			.Add(TEXT("spread_mode"), FToolParam::Enum(TEXT("random or even fan"), {TEXT("random"), TEXT("fan")}))
			.Add(TEXT("speed"), FToolParam::Number(TEXT("Projectile speed cm/s (default 2500)"), 1.0))
			.Add(TEXT("lifetime"), FToolParam::Number(TEXT("Projectile seconds (default 1.5)"), 0.01, 60.0))
			.Add(TEXT("range"), FToolParam::Number(TEXT("Hitscan range cm (default 3000)"), 1.0))
			.Add(TEXT("radius"), FToolParam::Number(TEXT("Projectile hit radius cm (default 12)"), 0.0))
			.Add(TEXT("knockback"), FToolParam::Number(TEXT("Push on hit (cm/s)"), 0.0))
			.Add(TEXT("muzzle"), FToolParam::Number(TEXT("Spawn distance in front of the shooter (default 60)")))
			.Add(TEXT("hits"), FToolParam::String(TEXT("What it can damage: tag:enemy, player or any (default any but the shooter)"), 128))
			.Add(TEXT("pool"), FToolParam::String(TEXT("Pool of bullet objects (default: a glowing bullet)"), 64))
			.Add(TEXT("pool_size"), FToolParam::Integer(TEXT("Default bullet pool size"), 1, 500))
			.Add(TEXT("bullet"), FToolParam::Object(TEXT("Default bullet look {size, color}"))))
		.Undoable().RequiresWorld()
		.Handle(&DefineWeapon);

	B.Tool(TEXT("equip"), TEXT("Give 'player' or nodes (tag:x) a weapon (empty weapon removes it)."))
		.Args(FToolSchema()
			.Add(TEXT("owner"), FToolParam::String(TEXT("'player', node or tag:x"), 256).Required())
			.Add(TEXT("weapon"), FToolParam::String(TEXT("Weapon name"), 64)))
		.Undoable().RequiresWorld()
		.Handle(&Equip);

	B.Tool(TEXT("damage"), TEXT("Damage 'player' or a node now (testing)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::String(TEXT("'player' or node"), 256).Required())
			.Add(TEXT("amount"), FToolParam::Number(TEXT("Damage")).Default(1.0))
			.Add(TEXT("type"), FToolParam::String(TEXT("Damage type"), 32)))
		.RequiresWorld()
		.Handle(&DamageTool);

	B.Tool(TEXT("spawn_player"), TEXT("Playable side-scrolling character with a follow camera (A/D run, Space jump). Level runs along +X, camera on +Y."))
		.Args(FToolSchema()
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("Start point (feet) [x,0,z]")).Required())
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("side: side-scroller (level along +X); topdown: WASD on the ground, aim with the mouse"), {TEXT("side"), TEXT("topdown")}))
			.Add(TEXT("reload_key"), FToolParam::String(TEXT("Key that reloads (optional, e.g. Q)"), 32))
			.Add(TEXT("body"), FToolParam::Target(TEXT("Optional model node (feet at z=0, facing +X); default: a hero")))
			.Add(TEXT("radius"), FToolParam::Number(TEXT("Collision radius cm"), 10.0, 300.0))
			.Add(TEXT("height"), FToolParam::Number(TEXT("Height cm"), 40.0, 600.0))
			.Add(TEXT("speed"), FToolParam::Number(TEXT("Run speed cm/s (default 650)"), 50.0, 5000.0))
			.Add(TEXT("jump"), FToolParam::Number(TEXT("Jump velocity cm/s (default 1250)"), 100.0, 10000.0))
			.Add(TEXT("jump_hold"), FToolParam::Number(TEXT("Seconds holding jump adds height (default 0.22)"), 0.0, 2.0))
			.Add(TEXT("gravity"), FToolParam::Number(TEXT("Gravity scale (default 2.8)"), 0.1, 10.0))
			.Add(TEXT("air_control"), FToolParam::Number(TEXT("0-1 (default 0.85)"), 0.0, 1.0))
			.Add(TEXT("camera_distance"), FToolParam::Number(TEXT("cm (default 1700)"), 100.0, 20000.0))
			.Add(TEXT("camera_height"), FToolParam::Number(TEXT("cm above the player (default 250)"), -5000.0, 5000.0))
			.Add(TEXT("camera_lead"), FToolParam::Number(TEXT("cm ahead in the facing direction (default 200)"), 0.0, 5000.0))
			.Add(TEXT("camera_pitch"), FToolParam::Number(TEXT("degrees (side default -6, top-down default 60 looking down)"), -89.0, 89.0))
			.Add(TEXT("fov"), FToolParam::Number(TEXT("Field of view (default 55)"), 10.0, 120.0))
			.Add(TEXT("kill_z"), FToolParam::Number(TEXT("Falling below this fires 'fall' (default start z - 1500)"))))
		.Undoable().RequiresWorld()
		.Handle(&SpawnPlayerTool);

	B.Tool(TEXT("remove_player"), TEXT("Remove the playable character and give the view back."))
		.Args(FToolSchema())
		.Undoable().RequiresWorld()
		.Handle(&RemovePlayerTool);

	B.Tool(TEXT("set_motion"), TEXT("Make nodes move while the game runs: patrol back and forth, follow a path, bob, and/or spin. Reset on restart."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), FToolParam::StringArray(TEXT("Node ids/paths/names or tag:x"), 500).Required())
			.Add(TEXT("motion"), FToolParam::Object(TEXT("{type: patrol|path|bob|none, axis:[1,0,0], distance, speed, points:[[dx,dy,dz]..], loop, amplitude, period, spin:[p,y,r] deg/s, face, offset}")).Required()))
		.Undoable().RequiresWorld()
		.Handle(&SetMotion);

	B.Tool(TEXT("clear_motion"), TEXT("Stop moving these nodes."))
		.Args(FToolSchema().Add(TEXT("targets"), FToolParam::StringArray(TEXT("Node ids/paths/names or tag:x"), 500).Required()))
		.Undoable().RequiresWorld()
		.Handle(&ClearMotion);

	B.Tool(TEXT("set_solid"), TEXT("Whether nodes block the player (pickups and enemies: false; they still fire touch)."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), FToolParam::StringArray(TEXT("Node ids/paths/names or tag:x"), 500).Required())
			.Add(TEXT("solid"), FToolParam::Boolean(TEXT("Blocks movement")).Required()))
		.Undoable().RequiresWorld()
		.Handle(&SetSolid);


	B.Tool(TEXT("get_guide"), TEXT("READ FIRST: how to turn a scene into a playable game with variables, rules, UI and start/restart."))
		.Args(FToolSchema())
		.Handle(&GetGuide);

	B.Tool(TEXT("define_vars"), TEXT("Define game variables and their starting values (start/restart resets to these)."))
		.Args(FToolSchema()
			.Add(TEXT("vars"), FToolParam::Object(TEXT("{name: starting value} (number, text, bool, list, map)")).Required())
			.Add(TEXT("replace"), FToolParam::Boolean(TEXT("Remove all other variables")).Default(false)))
		.Undoable().RequiresWorld()
		.Handle(&DefineVars);

	B.Tool(TEXT("set_var"), TEXT("Change a variable while the game runs (fires var_changed rules)."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Variable"), 128).Required())
			.Add(TEXT("value"), FToolParam::Any(TEXT("New value (JSON)")))
			.Add(TEXT("expr"), FToolParam::String(TEXT("Or an expression to evaluate"), 4000)))
		.RequiresWorld()
		.Handle(&SetVar);

	B.Tool(TEXT("eval"), TEXT("Evaluate an expression against the current variables (test conditions and functions)."))
		.Args(FToolSchema().Add(TEXT("expr"), FToolParam::String(TEXT("Expression"), 4000).Required()))
		.RequiresWorld()
		.Handle(&Eval);

	B.Tool(TEXT("define_rules"), TEXT("Add or replace rules (by name): when <on> [target/key/filter] and <condition> then <actions>."))
		.Args(FToolSchema().Add(TEXT("rules"), FToolParam::Array(TEXT("Rules: {name, on, target, key, filter, condition, actions:[...], enabled}"),
			FToolParam::Object(TEXT("Rule")), 200).Required()))
		.Undoable().RequiresWorld()
		.Handle(&DefineRules);

	B.Tool(TEXT("remove_rules"), TEXT("Remove rules by name."))
		.Args(FToolSchema().Add(TEXT("names"), FToolParam::StringArray(TEXT("Rule names"), 200).Required()))
		.Undoable().RequiresWorld()
		.Handle(&RemoveRules);

	B.Tool(TEXT("list_rules"), TEXT("All rules with their full definitions."))
		.Args(FToolSchema())
		.RequiresWorld()
		.Handle(&ListRules);

	B.Tool(TEXT("set_data"), TEXT("Attach data to a node (and its children); click rules read it as event.data."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("data"), FToolParam::Object(TEXT("e.g. {index: 4}")).Required()))
		.Undoable().RequiresWorld()
		.Handle(&SetData);

	B.Tool(TEXT("define_ui"), TEXT("Add or replace on-screen text and buttons. Text is a live template: 'Score: {score}'. Buttons fire 'button' rules."))
		.Args(FToolSchema().Add(TEXT("items"), FToolParam::Array(TEXT("Items: {name, kind: text|button, text, anchor:[x,y] 0-1, size:[w,h], color:[r,g,b,a], background, font_size, visible_if}"),
			FToolParam::Object(TEXT("UI item")), 64).Required()))
		.Undoable().RequiresWorld()
		.Handle(&DefineUi);

	B.Tool(TEXT("remove_ui"), TEXT("Remove on-screen items by name."))
		.Args(FToolSchema().Add(TEXT("names"), FToolParam::StringArray(TEXT("Item names"), 64).Required()))
		.Undoable().RequiresWorld()
		.Handle(&RemoveUi);

	B.Tool(TEXT("set_input_mode"), TEXT("cursor: visible mouse cursor, clicks hit objects and buttons (board/puzzle games). game: normal play/fly."))
		.Args(FToolSchema()
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("Mode"), {TEXT("cursor"), TEXT("game")}).Default(TEXT("cursor")))
			.Add(TEXT("lock_camera"), FToolParam::Boolean(TEXT("Stop mouse-look and movement (fixed board view)")).Default(false)))
		.Undoable().RequiresWorld()
		.Handle(&SetInputMode);

	B.Tool(TEXT("start"), TEXT("Start or restart the game: reset variables, remove objects made during play, clear timers, run 'start' rules."))
		.Args(FToolSchema())
		.RequiresWorld()
		.Handle(&Start);

	B.Tool(TEXT("stop"), TEXT("Stop running rules (definitions are kept)."))
		.Args(FToolSchema())
		.RequiresWorld()
		.Handle(&Stop);

	B.Tool(TEXT("fire_event"), TEXT("Simulate player input to test the game: a click on a node, a key, a button press or a custom event."))
		.Args(FToolSchema()
			.Add(TEXT("on"), FToolParam::Enum(TEXT("Event"), {TEXT("click"), TEXT("right_click"), TEXT("key"), TEXT("button"), TEXT("timer"), TEXT("event"), TEXT("touch"), TEXT("fall")}).Required())
			.Add(TEXT("target"), FToolParam::Target(TEXT("click/touch: the node")))
			.Add(TEXT("key"), FToolParam::String(TEXT("key: key name"), 64))
			.Add(TEXT("name"), FToolParam::String(TEXT("button/timer/event name"), 128))
			.Add(TEXT("data"), FToolParam::Object(TEXT("event data; touch: {from_above, from_below}"))))
		.RequiresWorld()
		.Handle(&FireEvent);

	B.Tool(TEXT("step"), TEXT("Advance game time for timers and tick rules (testing)."))
		.Args(FToolSchema().Add(TEXT("seconds"), FToolParam::Number(TEXT("Seconds"), 0.0, 600.0).Default(1.0)))
		.RequiresWorld()
		.Handle(&Step);

	B.Tool(TEXT("get_state"), TEXT("Running state: variables, timers, rules, UI, objects made in play, and the recent event log with errors."))
		.Args(FToolSchema().Add(TEXT("log_limit"), FToolParam::Integer(TEXT("Log entries"), 0, 100).Default(20)))
		.RequiresWorld()
		.Handle(&GetState);

	B.Tool(TEXT("export"), TEXT("The whole game definition (vars, rules, ui, node data, input mode) as JSON."))
		.Args(FToolSchema())
		.RequiresWorld()
		.Handle(&Export);

	B.Tool(TEXT("import"), TEXT("Replace the game definition with one from logic.export."))
		.Args(FToolSchema().Add(TEXT("logic"), FToolParam::Object(TEXT("Definition from logic.export")).Required()))
		.Undoable().RequiresWorld()
		.Handle(&Import);
}
