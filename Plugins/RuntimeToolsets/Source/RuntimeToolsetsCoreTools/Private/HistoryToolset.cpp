#include "HistoryToolset.h"
#include "Helpers/ToolsCommon.h"

using namespace RuntimeTools;

static bool RejectInTransaction(FToolInvocation& Inv)
{
	if (Inv.GetHistory().IsInTransaction())
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("a transaction is open"), TEXT("Call history.end_transaction first"));
		return true;
	}
	return false;
}

static void Undo(FToolInvocation& Inv)
{
	if (RejectInTransaction(Inv))
	{
		return;
	}
	FRuntimeCommandContext Context = Inv.MakeCommandContext();
	FRuntimeCommandHistory& History = Inv.GetHistory();
	FRuntimeHistoryStepResult Step;
	if (Inv.Has(TEXT("to_checkpoint")))
	{
		const FString Name = Inv.GetString(TEXT("to_checkpoint"));
		if (!History.UndoToMarker(Context, Name, Step))
		{
			Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no checkpoint '%s' in undo history"), *Name), TEXT("See history.get_history"));
			return;
		}
	}
	else
	{
		if (History.NumUndo() == 0)
		{
			Inv.Fail(ERuntimeToolError::NothingToUndo, TEXT("nothing to undo"));
			return;
		}
		Step = History.Undo(Context, static_cast<int32>(Inv.GetInt(TEXT("count"), 1)));
	}
	TSharedRef<FJsonObject> Out = StepToJson(Step);
	Out->SetNumberField(TEXT("undo_left"), History.NumUndo());
	Out->SetNumberField(TEXT("redo_available"), History.NumRedo());
	Inv.Succeed(Out);
}

static void Redo(FToolInvocation& Inv)
{
	if (RejectInTransaction(Inv))
	{
		return;
	}
	FRuntimeCommandHistory& History = Inv.GetHistory();
	if (History.NumRedo() == 0)
	{
		Inv.Fail(ERuntimeToolError::NothingToUndo, TEXT("nothing to redo"));
		return;
	}
	FRuntimeCommandContext Context = Inv.MakeCommandContext();
	const FRuntimeHistoryStepResult Step = History.Redo(Context, static_cast<int32>(Inv.GetInt(TEXT("count"), 1)));
	TSharedRef<FJsonObject> Out = StepToJson(Step);
	Out->SetNumberField(TEXT("redo_left"), History.NumRedo());
	Inv.Succeed(Out);
}

static void GetHistory(FToolInvocation& Inv)
{
	Inv.Succeed(Inv.GetHistory().ToJson(Inv.GetLimit(), Inv.GetBool(TEXT("include_redo"), false)));
}

static void BeginTransaction(FToolInvocation& Inv)
{
	const int32 Depth = Inv.GetHistory().BeginTransaction(Inv.GetString(TEXT("label"), TEXT("transaction")));
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("depth"), Depth);
	Inv.Succeed(Out);
}

static void EndTransaction(FToolInvocation& Inv)
{
	FRuntimeCommandContext Context = Inv.MakeCommandContext();
	int64 EntryId = INDEX_NONE;
	int32 Count = 0;
	const bool bCommit = Inv.GetBool(TEXT("commit"), true);
	if (!Inv.GetHistory().EndTransaction(bCommit, Context, EntryId, Count))
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("no open transaction"));
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("result"), bCommit ? TEXT("committed") : TEXT("rolled_back"));
	Out->SetNumberField(TEXT("commands"), Count);
	Out->SetBoolField(TEXT("still_open"), Inv.GetHistory().IsInTransaction());
	if (EntryId != INDEX_NONE)
	{
		Out->SetNumberField(TEXT("history_id"), static_cast<double>(EntryId));
	}
	Inv.Succeed(Out);
}

static void Checkpoint(FToolInvocation& Inv)
{
	if (RejectInTransaction(Inv))
	{
		return;
	}
	FRuntimeCommandContext Context = Inv.MakeCommandContext();
	const FString Name = Inv.GetString(TEXT("name"));
	const int64 Id = Inv.GetHistory().Checkpoint(Name, Context);
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("checkpoint"), Name);
	Out->SetNumberField(TEXT("history_id"), static_cast<double>(Id));
	Inv.Succeed(Out);
}

static void Rollback(FToolInvocation& Inv)
{
	if (RejectInTransaction(Inv))
	{
		return;
	}
	FRuntimeCommandContext Context = Inv.MakeCommandContext();
	const FString Name = Inv.GetString(TEXT("name"));
	FRuntimeHistoryStepResult Step;
	if (!Inv.GetHistory().UndoToMarker(Context, Name, Step))
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no checkpoint '%s' in undo history"), *Name), TEXT("See history.get_history"));
		return;
	}
	TSharedRef<FJsonObject> Out = StepToJson(Step);
	Out->SetStringField(TEXT("at_checkpoint"), Name);
	Inv.Succeed(Out);
}

static void SetHistoryLimit(FToolInvocation& Inv)
{
	FRuntimeCommandContext Context = Inv.MakeCommandContext();
	Inv.GetHistory().SetLimit(static_cast<int32>(Inv.GetInt(TEXT("limit"), 200)), Context);
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("limit"), Inv.GetHistory().GetLimit());
	Out->SetNumberField(TEXT("undo_count"), Inv.GetHistory().NumUndo());
	Inv.Succeed(Out);
}

void FHistoryToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("undo"), TEXT("Undo the last N changes, or back to a checkpoint."))
		.Args(FToolSchema()
			.Add(TEXT("count"), FToolParam::Integer(TEXT("Changes to undo"), 1, 1000).Default(1))
			.Add(TEXT("to_checkpoint"), FToolParam::String(TEXT("Undo until this checkpoint"), 128)))
		.Handle(&Undo);

	B.Tool(TEXT("redo"), TEXT("Redo the last N undone changes."))
		.Args(FToolSchema().Add(TEXT("count"), FToolParam::Integer(TEXT("Changes to redo"), 1, 1000).Default(1)))
		.Handle(&Redo);

	B.Tool(TEXT("get_history"), TEXT("Undo stack (newest first), redo count, open transaction."))
		.Args(FToolSchema()
			.Add(TEXT("limit"), FToolParam::Limit())
			.Add(TEXT("include_redo"), FToolParam::Boolean(TEXT("Also list redo entries")).Default(false)))
		.Handle(&GetHistory);

	B.Tool(TEXT("begin_transaction"), TEXT("Group following changes into one undo step. Nests."))
		.Args(FToolSchema().Add(TEXT("label"), FToolParam::String(TEXT("Label shown in history"), 128)))
		.Handle(&BeginTransaction);

	B.Tool(TEXT("end_transaction"), TEXT("Close the transaction: commit as one step, or roll back its changes."))
		.Args(FToolSchema().Add(TEXT("commit"), FToolParam::Boolean(TEXT("false = undo everything in it")).Default(true)))
		.Handle(&EndTransaction);

	B.Tool(TEXT("checkpoint"), TEXT("Name the current state so rollback can return to it."))
		.Args(FToolSchema().Add(TEXT("name"), FToolParam::String(TEXT("Checkpoint name"), 128).Required()))
		.Handle(&Checkpoint);

	B.Tool(TEXT("rollback"), TEXT("Undo everything after a checkpoint."))
		.Args(FToolSchema().Add(TEXT("name"), FToolParam::String(TEXT("Checkpoint name"), 128).Required()))
		.Handle(&Rollback);

	B.Tool(TEXT("set_history_limit"), TEXT("Max undo entries for this session. Oldest entries become permanent."))
		.Args(FToolSchema().Add(TEXT("limit"), FToolParam::Integer(TEXT("Max entries"), 1, 10000).Required()))
		.Handle(&SetHistoryLimit);
}
