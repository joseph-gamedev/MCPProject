#include "LogicToolset.h"
#include "LogicRuntime.h"
#include "RuntimeCommand.h"
#include "RuntimeObjectResolver.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolHelpers.h"

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
	if (On == TEXT("click") || On == TEXT("right_click"))
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

void FLogicToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
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
			.Add(TEXT("on"), FToolParam::Enum(TEXT("Event"), {TEXT("click"), TEXT("right_click"), TEXT("key"), TEXT("button"), TEXT("timer"), TEXT("event")}).Required())
			.Add(TEXT("target"), FToolParam::Target(TEXT("click: the node clicked")))
			.Add(TEXT("key"), FToolParam::String(TEXT("key: key name"), 64))
			.Add(TEXT("name"), FToolParam::String(TEXT("button/timer/event name"), 128))
			.Add(TEXT("data"), FToolParam::Object(TEXT("event data"))))
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
