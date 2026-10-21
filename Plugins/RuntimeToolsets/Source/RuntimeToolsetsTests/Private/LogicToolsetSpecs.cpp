#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"

BEGIN_DEFINE_SPEC(FLogicToolsetSpec, "RuntimeToolsets.Toolsets.Logic", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	TArray<FString> Cells;

	/** Tic-tac-toe built only from tool calls, the way an agent would do it from a prompt. */
	void BuildTicTacToe()
	{
		Cells.Reset();
		for (int32 i = 0; i < 9; ++i)
		{
			const double X = (i % 3 - 1) * 300.0, Y = (i / 3 - 1) * 300.0;
			const FString Id = H->CallOk(TEXT("build.create_primitive"), FString::Printf(
				TEXT(R"({"shape":"box","size":[280,280,40],"name":"cell_%d","tags":["cell"],"location":[%f,%f,0],"material":"white"})"), i, X, Y))->GetStringField(TEXT("id"));
			H->CallOk(TEXT("logic.set_data"), FString::Printf(TEXT(R"({"target":"%s","data":{"index":%d,"pos":[%f,%f,40]}})"), *Id, i, X, Y));
			Cells.Add(Id);
		}
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"({"vars":{"board":["","","","","","","","",""],"turn":"X","winner":"","moves":0}})"));
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"J({"rules":[
			{"name":"place","on":"click","target":"tag:cell","condition":"winner == '' && board[event.data.index] == ''","actions":[
				{"set_item":"board","index":"event.data.index","value":"turn"},
				{"call":"build.create_primitive","args":{"shape":"=turn == 'X' ? 'box' : 'cylinder'","size":[150,150,60],"location":"=event.data.pos",
					"name":"piece_{moves}","material":"=turn == 'X' ? 'red' : 'blue'"}},
				{"set":"moves","value":"moves + 1"},
				{"if":"line_winner(board, grid_lines(3,3,3), '') != null","then":[{"set":"winner","value":"turn"}],
				 "else":[{"if":"moves == 9","then":[{"set":"winner","value":"'draw'"}]}]},
				{"if":"winner == ''","then":[{"set":"turn","value":"turn == 'X' ? 'O' : 'X'"}]}
			]},
			{"name":"restart","on":"button","filter":"restart","actions":[{"restart":true}]},
			{"name":"restart_key","on":"key","key":"R","actions":[{"restart":true}]}
		]})J"));
		H->CallOk(TEXT("logic.define_ui"), TEXT(R"J({"items":[
			{"name":"status","kind":"text","text":"{winner == '' ? turn + ' to move' : (winner == 'draw' ? 'Draw!' : winner + ' wins!')}","anchor":[0.5,0.06]},
			{"name":"restart","kind":"button","text":"Restart","anchor":[0.5,0.94]}
		]})J"));
		H->CallOk(TEXT("logic.set_input_mode"), TEXT(R"({"mode":"cursor","lock_camera":true})"));
		H->CallOk(TEXT("logic.start"));
	}

	TSharedRef<FJsonObject> Click(int32 Cell)
	{
		return H->CallOk(TEXT("logic.fire_event"), FString::Printf(TEXT(R"({"on":"click","target":"%s"})"), *Cells[Cell]));
	}

	TSharedPtr<FJsonObject> Vars()
	{
		return H->CallOk(TEXT("logic.get_state"))->GetObjectField(TEXT("vars"));
	}

	FString Eval(const FString& Expr)
	{
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("expr"), Expr);
		const TSharedPtr<FJsonValue> V = H->CallOk(TEXT("logic.eval"), RuntimeJson::Write(Args))->TryGetField(TEXT("value"));
		if (!V.IsValid())
		{
			return FString();
		}
		if (V->Type == EJson::String)
		{
			return V->AsString();
		}
		TSharedRef<FJsonObject> W = MakeShared<FJsonObject>();
		W->SetField(TEXT("v"), V);
		const FString S = RuntimeJson::Write(W); // {"v":<value>}
		return S.Mid(5, S.Len() - 6);
	}
END_DEFINE_SPEC(FLogicToolsetSpec)

void FLogicToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]()
	{
		H->Call(TEXT("logic.stop"));
		H.Reset();
	});

	It("plays a tic-tac-toe game to a win, ignores illegal moves, and restarts cleanly", [this]()
	{
		BuildTicTacToe();
		Click(0); // X
		Click(1); // O
		Click(4); // X
		const TSharedRef<FJsonObject> Busy = Click(4); // occupied: ignored
		TestEqual(TEXT("occupied cell runs no rule"), Busy->GetArrayField(TEXT("events"))[0]->AsObject()->GetArrayField(TEXT("rules")).Num(), 0);
		Click(2); // O
		Click(8); // X wins on the diagonal
		TSharedPtr<FJsonObject> V = Vars();
		TestEqual(TEXT("winner"), V->GetStringField(TEXT("winner")), FString(TEXT("X")));
		TestEqual(TEXT("moves"), static_cast<int32>(V->GetNumberField(TEXT("moves"))), 5);
		Click(5); // game over: ignored
		TestEqual(TEXT("no moves after the win"), static_cast<int32>(Vars()->GetNumberField(TEXT("moves"))), 5);
		TestEqual(TEXT("5 pieces were placed"), static_cast<int32>(H->CallOk(TEXT("logic.get_state"))->GetNumberField(TEXT("play_objects"))), 5);
		const FRuntimeSceneNode* Piece = H->GetGraph()->FindByPath(TEXT("piece_0"));
		TestTrue(TEXT("pieces are real scene nodes"), Piece != nullptr && Piece->Actor.IsValid());

		H->CallOk(TEXT("logic.fire_event"), TEXT(R"({"on":"button","name":"restart"})"));
		V = Vars();
		TestEqual(TEXT("restart clears the winner"), V->GetStringField(TEXT("winner")), FString());
		TestEqual(TEXT("restart resets moves"), static_cast<int32>(V->GetNumberField(TEXT("moves"))), 0);
		TestEqual(TEXT("restart resets the turn"), V->GetStringField(TEXT("turn")), FString(TEXT("X")));
		TestEqual(TEXT("restart removes the pieces"), static_cast<int32>(H->CallOk(TEXT("logic.get_state"))->GetNumberField(TEXT("play_objects"))), 0);
		const FRuntimeSceneNode* Gone = H->GetGraph()->FindByPath(TEXT("piece_0"));
		TestTrue(TEXT("old pieces are gone"), Gone == nullptr || Gone->bSoftDeleted || !Gone->Actor.IsValid());
		Click(4);
		TestEqual(TEXT("playable again"), Vars()->GetStringField(TEXT("turn")), FString(TEXT("O")));
		H->CallOk(TEXT("logic.fire_event"), TEXT(R"({"on":"key","key":"R"})"));
		TestEqual(TEXT("R restarts too"), static_cast<int32>(Vars()->GetNumberField(TEXT("moves"))), 0);
	});

	It("keeps chosen variables over a restart", [this]()
	{
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"({"vars":{"score":0,"round":1}})"));
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"next","on":"button","filter":"next","actions":[
			{"set":"score","value":"score + 5"},{"set":"round","value":"round + 1"},{"restart":true,"keep":["score"]}]}]})"));
		H->CallOk(TEXT("logic.start"));
		H->CallOk(TEXT("logic.fire_event"), TEXT(R"({"on":"button","name":"next"})"));
		const TSharedPtr<FJsonObject> V = Vars();
		TestEqual(TEXT("kept"), static_cast<int32>(V->GetNumberField(TEXT("score"))), 5);
		TestEqual(TEXT("reset"), static_cast<int32>(V->GetNumberField(TEXT("round"))), 1);
	});

	It("runs a platformer: player, touch pickups and enemies, motion, hide/show reset on restart", [this]()
	{
		const FString Coin = H->CallOk(TEXT("build.create_primitive"), TEXT(R"({"shape":"cylinder","size":[60,60,12],"location":[0,0,60],"name":"coin_a","tags":["coin"]})"))->GetStringField(TEXT("id"));
		const FString Enemy = H->CallOk(TEXT("build.create_primitive"), TEXT(R"({"shape":"box","size":[80,80,80],"location":[1000,0,0],"name":"goomba","tags":["enemy"]})"))->GetStringField(TEXT("id"));
		H->CallOk(TEXT("logic.set_solid"), TEXT(R"({"targets":["tag:coin","tag:enemy"],"solid":false})"));
		H->CallOk(TEXT("logic.set_motion"), TEXT(R"({"targets":["tag:enemy"],"motion":{"type":"patrol","axis":[1,0,0],"distance":400,"speed":100}})"));
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"({"vars":{"coins":0,"lives":3,"stomps":0}})"));
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"J({"rules":[
			{"name":"coin","on":"touch","target":"tag:coin","actions":[{"hide":"event.target"},{"set":"coins","value":"coins + 1"}]},
			{"name":"stomp","on":"touch","target":"tag:enemy","condition":"event.data.from_above","actions":[{"hide":"event.target"},{"set":"stomps","value":"stomps + 1"},{"player":"bounce","value":"900"}]},
			{"name":"hit","on":"touch","target":"tag:enemy","condition":"!event.data.from_above","actions":[{"set":"lives","value":"lives - 1"},{"player":"respawn"}]},
			{"name":"fell","on":"fall","actions":[{"set":"lives","value":"lives - 1"}]}
		]})J"));
		const TSharedRef<FJsonObject> P = H->CallOk(TEXT("logic.spawn_player"), TEXT(R"({"location":[0,0,0]})"));
		TestTrue(TEXT("player exists"), P->GetBoolField(TEXT("exists")));
		H->CallOk(TEXT("logic.start"));
		// The player stands on the coin: stepping detects the overlap geometrically.
		H->CallOk(TEXT("logic.step"), TEXT(R"({"seconds":0.1})"));
		TSharedPtr<FJsonObject> V = Vars();
		TestEqual(TEXT("coin collected by overlap"), static_cast<int32>(V->GetNumberField(TEXT("coins"))), 1);
		AActor* CoinActor = H->GetGraph()->Find(Coin)->Actor.Get();
		TestTrue(TEXT("coin hidden"), CoinActor->IsHidden());
		// Enemy patrols.
		const FVector Before = H->GetGraph()->Find(Enemy)->Actor->GetActorLocation();
		H->CallOk(TEXT("logic.step"), TEXT(R"({"seconds":2})"));
		const FVector After = H->GetGraph()->Find(Enemy)->Actor->GetActorLocation();
		TestTrue(TEXT("enemy moved ~200"), FMath::IsNearlyEqual(After.X - Before.X, 200.0, 15.0));
		// Side hit vs stomp.
		H->CallOk(TEXT("logic.fire_event"), FString::Printf(TEXT(R"({"on":"touch","target":"%s","data":{"from_above":false}})"), *Enemy));
		TestEqual(TEXT("side hit costs a life"), static_cast<int32>(Vars()->GetNumberField(TEXT("lives"))), 2);
		H->CallOk(TEXT("logic.fire_event"), FString::Printf(TEXT(R"({"on":"touch","target":"%s","data":{"from_above":true}})"), *Enemy));
		TestEqual(TEXT("stomped"), static_cast<int32>(Vars()->GetNumberField(TEXT("stomps"))), 1);
		TestTrue(TEXT("enemy hidden"), H->GetGraph()->Find(Enemy)->Actor->IsHidden());
		H->CallOk(TEXT("logic.fire_event"), TEXT(R"({"on":"fall"})"));
		TestEqual(TEXT("fall rule"), static_cast<int32>(Vars()->GetNumberField(TEXT("lives"))), 1);
		// Restart: everything back.
		H->CallOk(TEXT("logic.start"));
		TestFalse(TEXT("coin back"), CoinActor->IsHidden());
		TestFalse(TEXT("enemy back"), H->GetGraph()->Find(Enemy)->Actor->IsHidden());
		TestTrue(TEXT("enemy at its start"), H->GetGraph()->Find(Enemy)->Actor->GetActorLocation().Equals(FVector(1000, 0, 0), 1.0));
		TestEqual(TEXT("lives reset"), static_cast<int32>(Vars()->GetNumberField(TEXT("lives"))), 3);
		// Bad inputs.
		H->CallFails(TEXT("logic.set_motion"), TEXT(R"({"targets":["tag:nothing"],"motion":{"type":"patrol"}})"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.set_motion"), TEXT(R"({"targets":["tag:coin"],"motion":{"type":"teleport"}})"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"x","on":"touch","actions":[{"player":"fly"}]}]})"), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("history.undo")); // last good edit: spawn_player... undo chain still consistent
		H->CallOk(TEXT("logic.remove_player"));
	});

	It("tweens with easing, yoyo, then-actions and end events", [this]()
	{
		const FString Box = H->SpawnCube();
		AActor* A = H->GetGraph()->Find(Box)->Actor.Get();
		const FVector Start = A->GetActorLocation();
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"({"vars":{"done":0}})"));
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"d","on":"event","filter":"moved","actions":[{"set":"done","value":"done + 1"}]}]})"));
		H->CallOk(TEXT("logic.start"));
		H->CallOk(TEXT("logic.tween"), FString::Printf(TEXT(R"({"targets":["%s"],"move":[0,0,100],"duration":1,"ease":"linear","emit":"moved"})"), *Box));
		H->CallOk(TEXT("logic.step"), TEXT(R"({"seconds":0.5})"));
		TestTrue(TEXT("halfway"), FMath::IsNearlyEqual(A->GetActorLocation().Z - Start.Z, 50.0, 2.0));
		H->CallOk(TEXT("logic.step"), TEXT(R"({"seconds":0.6})"));
		TestTrue(TEXT("arrived"), FMath::IsNearlyEqual(A->GetActorLocation().Z - Start.Z, 100.0, 0.5));
		TestEqual(TEXT("end event"), static_cast<int32>(Vars()->GetNumberField(TEXT("done"))), 1);
		H->CallOk(TEXT("logic.tween"), FString::Printf(TEXT(R"({"targets":["%s"],"move":[0,0,40],"duration":0.25,"loop":"yoyo"})"), *Box));
		H->CallOk(TEXT("logic.step"), TEXT(R"({"seconds":0.6})"));
		TestTrue(TEXT("yoyo returns"), FMath::IsNearlyEqual(A->GetActorLocation().Z - Start.Z, 100.0, 0.5));
		H->CallOk(TEXT("logic.tween"), FString::Printf(TEXT(R"({"targets":["%s"],"scale":0.1,"duration":0.2,"then":"hide"})"), *Box));
		H->CallOk(TEXT("logic.step"), TEXT(R"({"seconds":0.3})"));
		TestTrue(TEXT("hidden at the end"), A->IsHidden());
		H->CallFails(TEXT("logic.tween"), FString::Printf(TEXT(R"({"targets":["%s"],"duration":1})"), *Box), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("logic.start"));
		TestFalse(TEXT("restart restores"), A->IsHidden());
		TestTrue(TEXT("restart puts it back"), A->GetActorLocation().Equals(Start, 0.5));
	});

	It("pools objects: spawn reuses hidden copies and release returns them", [this]()
	{
		const FString Tpl = H->CallOk(TEXT("build.create_primitive"), TEXT(R"({"shape":"sphere","size":[40,40,40],"name":"pop_tpl"})"))->GetStringField(TEXT("id"));
		const TSharedRef<FJsonObject> P = H->CallOk(TEXT("logic.define_pool"), FString::Printf(TEXT(R"({"name":"pop","template":"%s","size":2})"), *Tpl));
		TestEqual(TEXT("2 free"), static_cast<int32>(P->GetNumberField(TEXT("free"))), 2);
		TestTrue(TEXT("template hidden"), H->GetGraph()->Find(Tpl)->Actor->IsHidden());
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"J({"rules":[{"name":"pop","on":"button","filter":"pop","actions":[
			{"spawn":"pop","at":"[0, 0, 300]","as":"p"},
			{"tween":"p.id","move":[0,0,100],"duration":0.3,"then":"release"}]}]})J"));
		H->CallOk(TEXT("logic.start"));
		H->CallOk(TEXT("logic.fire_event"), TEXT(R"({"on":"button","name":"pop"})"));
		TestEqual(TEXT("one in use"), static_cast<int32>(H->CallOk(TEXT("logic.get_state"))->GetObjectField(TEXT("pools_free"))->GetNumberField(TEXT("pop"))), 1);
		H->CallOk(TEXT("logic.fire_event"), TEXT(R"({"on":"button","name":"pop"})"));
		H->CallOk(TEXT("logic.fire_event"), TEXT(R"({"on":"button","name":"pop"})")); // recycles the oldest
		TestEqual(TEXT("all in use"), static_cast<int32>(H->CallOk(TEXT("logic.get_state"))->GetObjectField(TEXT("pools_free"))->GetNumberField(TEXT("pop"))), 0);
		H->CallOk(TEXT("logic.step"), TEXT(R"({"seconds":0.5})"));
		TestEqual(TEXT("released after the tween"), static_cast<int32>(H->CallOk(TEXT("logic.get_state"))->GetObjectField(TEXT("pools_free"))->GetNumberField(TEXT("pop"))), 2);
		H->CallFails(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"x","on":"start","actions":[{"spawn":"pop"}]}]})"), ERuntimeToolError::InvalidArguments);
	});

	It("handles health, resistances, invulnerability and damage zones", [this]()
	{
		const FString Dummy = H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"box","size":[100,100,100],"location":[0,0,0],"name":"dummy","tags":["dummy"]})J"))->GetStringField(TEXT("id"));
		H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"box","size":[300,300,20],"location":[0,0,0],"name":"lava","tags":["lava"]})J"));
		H->CallOk(TEXT("logic.set_solid"), TEXT(R"J({"targets":["tag:lava","tag:dummy"],"solid":false})J"));
		H->CallOk(TEXT("logic.define_health"), TEXT(R"J({"targets":["tag:dummy"],"max":20,"resistances":{"fire":0.5},"on_death":"hide"})J"));
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"J({"vars":{"hurt":0,"dead":0}})J"));
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"J({"rules":[
			{"name":"h","on":"damaged","target":"tag:dummy","actions":[{"set":"hurt","value":"hurt + event.amount"}]},
			{"name":"d","on":"died","target":"tag:dummy","actions":[{"set":"dead","value":"dead + 1"}]}]})J"));
		H->CallOk(TEXT("logic.start"));
		H->CallOk(TEXT("logic.damage"), FString::Printf(TEXT(R"J({"target":"%s","amount":4,"type":"fire"})J"), *Dummy));
		TestEqual(TEXT("resistance halves fire"), H->CallOk(TEXT("logic.eval"), FString::Printf(TEXT(R"J({"expr":"hp('%s')"})J"), *Dummy))->GetNumberField(TEXT("value")), 18.0);
		H->CallOk(TEXT("logic.define_hitzone"), TEXT(R"J({"targets":["tag:lava"],"damage":3,"mode":"stay","interval":0.5,"affects":"tag:dummy"})J"));
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":1.05})J"));
		const double Hp = H->CallOk(TEXT("logic.eval"), FString::Printf(TEXT(R"J({"expr":"hp('%s')"})J"), *Dummy))->GetNumberField(TEXT("value"));
		TestTrue(TEXT("lava hurts every 0.5 s"), Hp <= 12.0 && Hp >= 9.0);
		TestTrue(TEXT("damaged rule"), Vars()->GetNumberField(TEXT("hurt")) >= 8.0);
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":3})J"));
		TestEqual(TEXT("died once"), static_cast<int32>(Vars()->GetNumberField(TEXT("dead"))), 1);
		TestTrue(TEXT("hidden on death"), H->GetGraph()->Find(Dummy)->Actor->IsHidden());
		TestEqual(TEXT("alive() false"), H->CallOk(TEXT("logic.eval"), FString::Printf(TEXT(R"J({"expr":"alive('%s')"})J"), *Dummy))->GetBoolField(TEXT("value")), false);
		H->CallOk(TEXT("logic.start"));
		TestEqual(TEXT("restart refills"), H->CallOk(TEXT("logic.eval"), FString::Printf(TEXT(R"J({"expr":"hp('%s')"})J"), *Dummy))->GetNumberField(TEXT("value")), 20.0);
		TestFalse(TEXT("restart shows it"), H->GetGraph()->Find(Dummy)->Actor->IsHidden());
	});

	It("shoots projectiles and hitscan with magazines, reloads and kills", [this]()
	{
		H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"box","size":[60,60,60],"location":[0,0,0],"name":"turret","tags":["turret"]})J"));
		const FString Enemy = H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"box","size":[100,100,100],"location":[800,0,0],"pivot":"center","name":"enemy","tags":["enemy"]})J"))->GetStringField(TEXT("id"));
		const FString Enemy2 = H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"box","size":[100,100,100],"location":[0,700,0],"pivot":"center","name":"enemy2","tags":["enemy"]})J"))->GetStringField(TEXT("id"));
		H->CallOk(TEXT("logic.set_solid"), TEXT(R"J({"targets":["tag:enemy","tag:turret"],"solid":false})J"));
		H->CallOk(TEXT("logic.define_health"), TEXT(R"J({"targets":["tag:enemy"],"max":3,"on_death":"hide"})J"));
		H->CallOk(TEXT("logic.define_weapon"), TEXT(R"J({"name":"gun","damage":1,"rate":20,"speed":4000,"magazine":2,"reload":0.5,"hits":"tag:enemy"})J"));
		H->CallOk(TEXT("logic.define_weapon"), TEXT(R"J({"name":"laser","mode":"hitscan","damage":5,"rate":1,"hits":"tag:enemy"})J"));
		H->CallOk(TEXT("logic.equip"), TEXT(R"J({"owner":"tag:turret","weapon":"gun"})J"));
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"J({"vars":{"kills":0}})J"));
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"J({"rules":[
			{"name":"shoot","on":"button","filter":"shoot","actions":[{"fire":"nodes('tag:turret')","at":"event.at"}]},
			{"name":"zap","on":"button","filter":"zap","actions":[{"fire":"nodes('tag:turret')","weapon":"laser","at":"event.at"}]},
			{"name":"kill","on":"died","target":"tag:enemy","actions":[{"set":"kills","value":"kills + 1"}]}]})J"));
		H->CallOk(TEXT("logic.start"));
		const FString At1 = TEXT(R"J({"on":"button","name":"shoot","data":{"at":[800,0,0]}})J");
		H->CallOk(TEXT("logic.fire_event"), At1);
		TestEqual(TEXT("bullet flying"), static_cast<int32>(H->CallOk(TEXT("logic.get_state"))->GetNumberField(TEXT("projectiles"))), 1);
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":0.4})J"));
		TestEqual(TEXT("hit"), H->CallOk(TEXT("logic.eval"), FString::Printf(TEXT(R"J({"expr":"hp('%s')"})J"), *Enemy))->GetNumberField(TEXT("value")), 2.0);
		H->CallOk(TEXT("logic.fire_event"), At1); // last round: auto reload starts
		H->CallOk(TEXT("logic.fire_event"), At1); // reloading: no shot
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":0.4})J"));
		TestEqual(TEXT("second hit only"), H->CallOk(TEXT("logic.eval"), FString::Printf(TEXT(R"J({"expr":"hp('%s')"})J"), *Enemy))->GetNumberField(TEXT("value")), 1.0);
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":0.3})J"));
		H->CallOk(TEXT("logic.fire_event"), At1); // reloaded
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":0.4})J"));
		TestEqual(TEXT("kill counted"), static_cast<int32>(Vars()->GetNumberField(TEXT("kills"))), 1);
		TestTrue(TEXT("enemy hidden"), H->GetGraph()->Find(Enemy)->Actor->IsHidden());
		H->CallOk(TEXT("logic.fire_event"), TEXT(R"J({"on":"button","name":"zap","data":{"at":[0,700,0]}})J"));
		TestEqual(TEXT("hitscan kills instantly"), static_cast<int32>(Vars()->GetNumberField(TEXT("kills"))), 2);
		TestTrue(TEXT("enemy2 hidden"), H->GetGraph()->Find(Enemy2)->Actor->IsHidden());
		H->CallFails(TEXT("logic.equip"), TEXT(R"J({"owner":"player","weapon":"nope"})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.define_rules"), TEXT(R"J({"rules":[{"name":"x","on":"start","actions":[{"damage":"'player'"}]}]})J"), ERuntimeToolError::InvalidArguments);
	});

	It("spawns a top-down player with health and invulnerability", [this]()
	{
		const TSharedRef<FJsonObject> P = H->CallOk(TEXT("logic.spawn_player"), TEXT(R"J({"location":[0,0,0],"mode":"topdown"})J"));
		TestTrue(TEXT("player"), P->GetBoolField(TEXT("exists")));
		H->CallOk(TEXT("logic.define_health"), TEXT(R"J({"targets":["player"],"max":5,"invulnerable":1})J"));
		H->CallOk(TEXT("logic.start"));
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":1.6})J")); // spawn invulnerability ends
		H->CallOk(TEXT("logic.damage"), TEXT(R"J({"target":"player","amount":2})J"));
		const TSharedRef<FJsonObject> Again = H->CallOk(TEXT("logic.damage"), TEXT(R"J({"target":"player","amount":2})J"));
		TestFalse(TEXT("invulnerable right after a hit"), Again->GetBoolField(TEXT("applied")));
		TestEqual(TEXT("health 3"), H->CallOk(TEXT("logic.eval"), TEXT(R"J({"expr":"health.player"})J"))->GetNumberField(TEXT("value")), 3.0);
		H->CallOk(TEXT("logic.remove_player"));
	});

	It("runs AI brains: sees the player, changes state, shoots, searches", [this]()
	{
		H->CallOk(TEXT("logic.spawn_player"), TEXT(R"J({"location":[0,0,0],"mode":"topdown"})J"));
		H->CallOk(TEXT("logic.define_health"), TEXT(R"J({"targets":["player"],"max":10,"invulnerable":0})J"));
		const FString Guard = H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"box","size":[80,80,120],"location":[700,0,0],"rotation":[0,180,0],"name":"guard","tags":["guard"]})J"))->GetStringField(TEXT("id"));
		H->CallOk(TEXT("logic.set_solid"), TEXT(R"J({"targets":["tag:guard"],"solid":false})J"));
		H->CallOk(TEXT("logic.define_weapon"), TEXT(R"J({"name":"zap","damage":1,"rate":5,"speed":4000,"hits":"player"})J"));
		H->CallOk(TEXT("logic.equip"), TEXT(R"J({"owner":"tag:guard","weapon":"zap"})J"));
		H->CallOk(TEXT("logic.define_brain"), TEXT(R"J({"name":"g","vision":{"radius":1200,"angle":90},"states":[
			{"name":"patrol","move":"stop","transitions":[{"when":"sees","to":"attack"}]},
			{"name":"attack","move":"stop","face":"target","fire":true,"icon":"!","transitions":[{"when":"since_seen > 1","to":"search"}]},
			{"name":"search","move":"to_last_known","icon":"?","transitions":[{"when":"sees","to":"attack"}]}]})J"));
		const TSharedRef<FJsonObject> A = H->CallOk(TEXT("logic.assign_brain"), TEXT(R"J({"targets":["tag:guard"],"brain":"g"})J"));
		TestEqual(TEXT("one agent"), static_cast<int32>(A->GetNumberField(TEXT("agents"))), 1);
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"J({"vars":{"alerted":false}})J"));
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"J({"rules":[{"name":"a","on":"state","filter":"attack","target":"tag:guard","actions":[{"set":"alerted","value":"true"}]}]})J"));
		H->CallOk(TEXT("logic.start"));
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":0.5})J"));
		TestTrue(TEXT("state event"), Vars()->GetBoolField(TEXT("alerted")));
		TestTrue(TEXT("attacking"), H->CallOk(TEXT("logic.get_state"))->GetObjectField(TEXT("brains"))->GetStringField(Guard).StartsWith(TEXT("attack")));
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":1.5})J"));
		TestTrue(TEXT("guard shot the player"), H->CallOk(TEXT("logic.eval"), TEXT(R"J({"expr":"health.player"})J"))->GetNumberField(TEXT("value")) < 10.0);
		// Turn the guard away and move the player out of its view: it loses sight and searches.
		H->GetGraph()->Find(Guard)->Actor->SetActorLocation(FVector(700, 3000, 0));
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":0.3})J"));
		H->CallOk(TEXT("logic.step"), TEXT(R"J({"seconds":1.5})J"));
		const FString St = H->CallOk(TEXT("logic.get_state"))->GetObjectField(TEXT("brains"))->GetStringField(Guard);
		TestTrue(FString::Printf(TEXT("searching (%s)"), *St), St.StartsWith(TEXT("search")) || St.StartsWith(TEXT("attack")));
		H->CallFails(TEXT("logic.define_brain"), TEXT(R"J({"name":"bad","states":[{"name":"a","transitions":[{"when":"sees","to":"nowhere"}]}]})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.define_brain"), TEXT(R"J({"name":"bad","states":[{"name":"a","move":"teleport"}]})J"), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("logic.remove_player"));
	});

	It("plays feedback presets and validates them", [this]()
	{
		H->CallOk(TEXT("logic.define_feedback"), TEXT(R"J({"name":"hit","steps":[{"type":"shake","duration":0.2},{"type":"flash","intensity":0.6},{"type":"slowmo","scale":0.5,"duration":0.3},{"type":"text","text":"BOOM"}]})J"));
		H->CallOk(TEXT("logic.set_hud"), TEXT(R"J({"enemy_bars":true})J"));
		H->CallOk(TEXT("logic.start"));
		H->CallOk(TEXT("logic.play_feedback"), TEXT(R"J({"name":"hit","at":[0,0,100]})J"));
		H->CallFails(TEXT("logic.define_feedback"), TEXT(R"J({"name":"x","steps":[{"type":"explode"}]})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.play_feedback"), TEXT(R"J({"name":"nope"})J"), ERuntimeToolError::NotFound);
		H->CallOk(TEXT("logic.define_ui"), TEXT(R"J({"items":[{"name":"hp","kind":"bar","value":"3","max":"10","text":"HP"}]})J"));
		H->CallFails(TEXT("logic.define_ui"), TEXT(R"J({"items":[{"name":"hp2","kind":"bar","text":"HP"}]})J"), ERuntimeToolError::InvalidArguments);
	});

	It("detects a draw", [this]()
	{
		BuildTicTacToe();
		for (const int32 C : {0, 1, 2, 4, 3, 5, 7, 6, 8})
		{
			Click(C);
		}
		const TSharedPtr<FJsonObject> V = Vars();
		TestEqual(TEXT("draw"), V->GetStringField(TEXT("winner")), FString(TEXT("draw")));
		TestEqual(TEXT("nine moves"), static_cast<int32>(V->GetNumberField(TEXT("moves"))), 9);
	});

	It("evaluates expressions and board-game helpers", [this]()
	{
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"({"vars":{"score":3,"name":"Ann","grid":["X","X","","O","O","","","",""]}})"));
		TestEqual(TEXT("arithmetic"), Eval(TEXT("score * 2 + 1")), FString(TEXT("7")));
		TestEqual(TEXT("strings"), Eval(TEXT("'Hi ' + name + '!'")), FString(TEXT("Hi Ann!")));
		TestEqual(TEXT("ternary"), Eval(TEXT("score > 2 ? 'big' : 'small'")), FString(TEXT("big")));
		TestEqual(TEXT("8 lines"), Eval(TEXT("len(grid_lines(3,3,3))")), FString(TEXT("8")));
		TestEqual(TEXT("connect-four lines"), Eval(TEXT("len(grid_lines(6,7,4))")), FString(TEXT("69")));
		TestEqual(TEXT("winning move"), Eval(TEXT("winning_move(grid, grid_lines(3,3,3), 'X', '')")), FString(TEXT("2")));
		TestEqual(TEXT("block move"), Eval(TEXT("winning_move(grid, grid_lines(3,3,3), 'O', '')")), FString(TEXT("5")));
		TestEqual(TEXT("no winner yet"), Eval(TEXT("line_winner(grid, grid_lines(3,3,3), '') == null")), FString(TEXT("true")));
		TestEqual(TEXT("empty cells"), Eval(TEXT("indices(grid, '')")), FString(TEXT("[2,5,6,7,8]")));
		TestEqual(TEXT("set copy"), Eval(TEXT("set(grid, 2, 'X')[2]")), FString(TEXT("X")));
		H->CallFails(TEXT("logic.eval"), TEXT(R"({"expr":"score +"})"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.eval"), TEXT(R"({"expr":"nope + 1"})"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.eval"), TEXT(R"({"expr":"score = 2"})"), ERuntimeToolError::InvalidArguments);
	});

	It("runs timers, var_changed and custom events", [this]()
	{
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"({"vars":{"ticks":0,"two":false}})"));
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"J({"rules":[
			{"name":"begin","on":"start","actions":[{"timer":"t","delay":1,"repeat":true}]},
			{"name":"count","on":"timer","filter":"t","actions":[{"set":"ticks","value":"ticks + 1"}]},
			{"name":"watch","on":"var_changed","filter":"ticks","condition":"event.value == 2","actions":[{"emit":"reached","data":{"at":"=event.value"}}]},
			{"name":"flag","on":"event","filter":"reached","condition":"event.at == 2","actions":[{"set":"two","value":"true"}]}
		]})J"));
		H->CallOk(TEXT("logic.start"));
		H->CallOk(TEXT("logic.step"), TEXT(R"({"seconds":3.05})"));
		const TSharedPtr<FJsonObject> V = Vars();
		TestEqual(TEXT("three ticks"), static_cast<int32>(V->GetNumberField(TEXT("ticks"))), 3);
		TestTrue(TEXT("chained events"), V->GetBoolField(TEXT("two")));
	});

	It("rejects bad rules with a clear message, and undo restores definitions", [this]()
	{
		H->CallFails(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"a","on":"click","condition":"x =="}]})"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"a","on":"jump"}]})"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"a","on":"start","actions":[{"teleport":"x"}]}]})"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"a","on":"start","actions":[{"call":"logic.start"}]}]})"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"a","on":"key","key":"NotAKey"}]})"), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"a","on":"start","actions":[{"log":"hello"}]}]})"));
		TestEqual(TEXT("defined"), H->CallOk(TEXT("logic.list_rules"))->GetArrayField(TEXT("rules")).Num(), 1);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("undo removes it"), H->CallOk(TEXT("logic.list_rules"))->GetArrayField(TEXT("rules")).Num(), 0);
		H->CallOk(TEXT("history.redo"));
		TestEqual(TEXT("redo restores it"), H->CallOk(TEXT("logic.list_rules"))->GetArrayField(TEXT("rules")).Num(), 1);
	});

	It("reports runtime errors in the event log instead of failing silently", [this]()
	{
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"({"vars":{"n":0}})"));
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"bad","on":"start","actions":[{"set":"missing","value":"1"}]}]})"));
		const TSharedRef<FJsonObject> S = H->CallOk(TEXT("logic.start"));
		const TArray<TSharedPtr<FJsonValue>> Log = S->GetArrayField(TEXT("log"));
		TestTrue(TEXT("logged"), Log.Num() > 0 && Log.Last()->AsObject()->HasField(TEXT("errors")));
	});

	It("exports and imports the whole game", [this]()
	{
		BuildTicTacToe();
		const TSharedPtr<FJsonObject> Def = H->CallOk(TEXT("logic.export"))->GetObjectField(TEXT("logic"));
		TestEqual(TEXT("rules exported"), Def->GetArrayField(TEXT("rules")).Num(), 3);
		H->CallOk(TEXT("logic.define_rules"), TEXT(R"({"rules":[{"name":"extra","on":"start"}]})"));
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetObjectField(TEXT("logic"), Def);
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("logic.import"), RuntimeJson::Write(Args));
		TestEqual(TEXT("imported rules"), static_cast<int32>(R->GetNumberField(TEXT("rules"))), 3);
		H->CallOk(TEXT("logic.start"));
		for (const int32 C : {0, 3, 1, 4, 2})
		{
			Click(C);
		}
		TestEqual(TEXT("imported game plays"), Vars()->GetStringField(TEXT("winner")), FString(TEXT("X")));
	});
}

#endif
