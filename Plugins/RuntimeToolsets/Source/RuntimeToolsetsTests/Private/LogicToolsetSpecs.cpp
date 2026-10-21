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
