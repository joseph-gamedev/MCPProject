#include "RuntimeCommandHistory.h"
#include "RuntimeToolsetsLog.h"

// ---------------------------------------------------------------------------------------------------------------------
// FCompositeCommand
// ---------------------------------------------------------------------------------------------------------------------

bool FCompositeCommand::Do(FRuntimeCommandContext& Context, FString& OutError)
{
	for (int32 i = 0; i < Commands.Num(); ++i)
	{
		if (!Commands[i]->Do(Context, OutError))
		{
			// Roll back what was applied so the composite is all-or-nothing.
			for (int32 j = i - 1; j >= 0; --j)
			{
				Commands[j]->Undo(Context);
			}
			for (int32 j = 0; j < i; ++j)
			{
				Commands[j]->OnDiscarded(Context, true);
			}
			OutError = FString::Printf(TEXT("step %d (%s): %s"), i + 1, *Commands[i]->GetLabel(), *OutError);
			return false;
		}
	}
	return true;
}

bool FCompositeCommand::Undo(FRuntimeCommandContext& Context)
{
	bool bAll = true;
	for (int32 i = Commands.Num() - 1; i >= 0; --i)
	{
		bAll &= Commands[i]->Undo(Context);
	}
	return bAll;
}

bool FCompositeCommand::Redo(FRuntimeCommandContext& Context)
{
	bool bAll = true;
	for (TUniquePtr<IRuntimeCommand>& C : Commands)
	{
		bAll &= C->Redo(Context);
	}
	return bAll;
}

TSharedPtr<FJsonObject> FCompositeCommand::GetDelta(bool bForUndo) const
{
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("label"), Label);
	Out->SetNumberField(TEXT("steps"), Commands.Num());
	constexpr int32 MaxDetail = 8;
	TArray<TSharedPtr<FJsonValue>> Items;
	for (int32 i = 0; i < Commands.Num() && i < MaxDetail; ++i)
	{
		TSharedPtr<FJsonObject> D = Commands[i]->GetDelta(bForUndo);
		if (D.IsValid())
		{
			Items.Add(MakeShared<FJsonValueObject>(D));
		}
	}
	if (Items.Num() > 0)
	{
		Out->SetArrayField(TEXT("changes"), Items);
	}
	return Out;
}

void FCompositeCommand::OnDiscarded(FRuntimeCommandContext& Context, bool bWasUndone)
{
	for (TUniquePtr<IRuntimeCommand>& C : Commands)
	{
		C->OnDiscarded(Context, bWasUndone);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FRuntimeCommandHistory
// ---------------------------------------------------------------------------------------------------------------------

FRuntimeCommandHistory::~FRuntimeCommandHistory()
{
	// Commands must be discarded through Clear() with a valid context; anything left is simply dropped.
	if (UndoStack.Num() > 0 || RedoStack.Num() > 0 || OpenTransaction.IsValid())
	{
		UE_LOG(LogRuntimeToolsetsCore, Verbose, TEXT("History destroyed with %d undo / %d redo entries"), UndoStack.Num(), RedoStack.Num());
	}
}

int64 FRuntimeCommandHistory::Execute(TUniquePtr<IRuntimeCommand> Command, FRuntimeCommandContext& Context, FString& OutError, TSharedPtr<FJsonObject>& OutDelta)
{
	check(Command.IsValid());
	if (!Command->Do(Context, OutError))
	{
		return INDEX_NONE;
	}
	OutDelta = Command->GetDelta(false);

	if (OpenTransaction.IsValid())
	{
		ClearRedo(Context);
		OpenTransaction->AddApplied(MoveTemp(Command));
		return OpenTransactionId;
	}

	TUniquePtr<FRuntimeHistoryEntry> Entry = MakeUnique<FRuntimeHistoryEntry>();
	Entry->Id = NextId++;
	Entry->Kind = ERuntimeHistoryEntryKind::Command;
	Entry->Label = Command->GetLabel();
	Entry->Time = FDateTime::UtcNow();
	Entry->Command = MoveTemp(Command);
	const int64 Id = Entry->Id;
	Push(MoveTemp(Entry), Context);
	return Id;
}

void FRuntimeCommandHistory::RecordIrreversible(const FString& Label, FRuntimeCommandContext& Context)
{
	if (OpenTransaction.IsValid())
	{
		int64 Ignored;
		int32 IgnoredCount;
		CommitTransaction(Context, Ignored, IgnoredCount);
		TransactionDepth = 0;
	}
	TUniquePtr<FRuntimeHistoryEntry> Entry = MakeUnique<FRuntimeHistoryEntry>();
	Entry->Id = NextId++;
	Entry->Kind = ERuntimeHistoryEntryKind::Irreversible;
	Entry->Label = Label;
	Entry->Time = FDateTime::UtcNow();
	Push(MoveTemp(Entry), Context);
}

void FRuntimeCommandHistory::Push(TUniquePtr<FRuntimeHistoryEntry> Entry, FRuntimeCommandContext& Context)
{
	ClearRedo(Context);
	UndoStack.Add(MoveTemp(Entry));
	Trim(Context);
}

void FRuntimeCommandHistory::ClearRedo(FRuntimeCommandContext& Context)
{
	for (int32 i = RedoStack.Num() - 1; i >= 0; --i)
	{
		if (RedoStack[i]->Command.IsValid())
		{
			RedoStack[i]->Command->OnDiscarded(Context, true);
		}
	}
	RedoStack.Reset();
}

void FRuntimeCommandHistory::Trim(FRuntimeCommandContext& Context)
{
	while (UndoStack.Num() > Limit)
	{
		TUniquePtr<FRuntimeHistoryEntry> Oldest = MoveTemp(UndoStack[0]);
		UndoStack.RemoveAt(0);
		if (Oldest->Command.IsValid())
		{
			Oldest->Command->OnDiscarded(Context, false);
		}
	}
}

bool FRuntimeCommandHistory::UndoOne(FRuntimeCommandContext& Context, FRuntimeHistoryStepResult& Result)
{
	if (UndoStack.Num() == 0)
	{
		return false;
	}
	TUniquePtr<FRuntimeHistoryEntry> Entry = UndoStack.Pop();
	switch (Entry->Kind)
	{
	case ERuntimeHistoryEntryKind::Marker:
		Result.PassedMarkers.Add(Entry->Label);
		break;
	case ERuntimeHistoryEntryKind::Irreversible:
		Result.SkippedIrreversible.Add(Entry->Label);
		break;
	case ERuntimeHistoryEntryKind::Command:
		if (Entry->Command->Undo(Context))
		{
			TSharedPtr<FJsonObject> D = Entry->Command->GetDelta(true);
			if (D.IsValid())
			{
				Result.Deltas.Add(D);
			}
		}
		else
		{
			Result.Stale.Add(Entry->Label);
		}
		++Result.Applied;
		break;
	}
	RedoStack.Add(MoveTemp(Entry));
	return true;
}

FRuntimeHistoryStepResult FRuntimeCommandHistory::Undo(FRuntimeCommandContext& Context, int32 Count)
{
	FRuntimeHistoryStepResult Result;
	if (OpenTransaction.IsValid())
	{
		return Result; // caller reports wrong_state
	}
	while (Result.Applied < Count && UndoOne(Context, Result))
	{
	}
	return Result;
}

bool FRuntimeCommandHistory::UndoToMarker(FRuntimeCommandContext& Context, const FString& Marker, FRuntimeHistoryStepResult& OutResult)
{
	if (!HasMarker(Marker) || OpenTransaction.IsValid())
	{
		return false;
	}
	while (UndoStack.Num() > 0)
	{
		const FRuntimeHistoryEntry& Top = *UndoStack.Last();
		if (Top.Kind == ERuntimeHistoryEntryKind::Marker && Top.Label == Marker)
		{
			break;
		}
		UndoOne(Context, OutResult);
	}
	return true;
}

FRuntimeHistoryStepResult FRuntimeCommandHistory::Redo(FRuntimeCommandContext& Context, int32 Count)
{
	FRuntimeHistoryStepResult Result;
	if (OpenTransaction.IsValid())
	{
		return Result;
	}
	while (Result.Applied < Count && RedoStack.Num() > 0)
	{
		TUniquePtr<FRuntimeHistoryEntry> Entry = RedoStack.Pop();
		switch (Entry->Kind)
		{
		case ERuntimeHistoryEntryKind::Marker:
			Result.PassedMarkers.Add(Entry->Label);
			break;
		case ERuntimeHistoryEntryKind::Irreversible:
			Result.SkippedIrreversible.Add(Entry->Label);
			break;
		case ERuntimeHistoryEntryKind::Command:
			if (Entry->Command->Redo(Context))
			{
				TSharedPtr<FJsonObject> D = Entry->Command->GetDelta(false);
				if (D.IsValid())
				{
					Result.Deltas.Add(D);
				}
			}
			else
			{
				Result.Stale.Add(Entry->Label);
			}
			++Result.Applied;
			break;
		}
		UndoStack.Add(MoveTemp(Entry));
	}
	// Markers sitting directly on top of the redo stack belong after the last redone command.
	while (RedoStack.Num() > 0 && RedoStack.Last()->Kind == ERuntimeHistoryEntryKind::Marker)
	{
		UndoStack.Add(RedoStack.Pop());
	}
	return Result;
}

int64 FRuntimeCommandHistory::Checkpoint(const FString& Name, FRuntimeCommandContext& Context)
{
	TUniquePtr<FRuntimeHistoryEntry> Entry = MakeUnique<FRuntimeHistoryEntry>();
	Entry->Id = NextId++;
	Entry->Kind = ERuntimeHistoryEntryKind::Marker;
	Entry->Label = Name;
	Entry->Time = FDateTime::UtcNow();
	const int64 Id = Entry->Id;
	// Markers do not invalidate redo... but a checkpoint after undo makes redo ambiguous, so clear it like a command.
	Push(MoveTemp(Entry), Context);
	return Id;
}

bool FRuntimeCommandHistory::HasMarker(const FString& Name) const
{
	for (const TUniquePtr<FRuntimeHistoryEntry>& E : UndoStack)
	{
		if (E->Kind == ERuntimeHistoryEntryKind::Marker && E->Label == Name)
		{
			return true;
		}
	}
	return false;
}

int32 FRuntimeCommandHistory::BeginTransaction(const FString& Label)
{
	if (TransactionDepth == 0)
	{
		OpenTransaction = MakeUnique<FCompositeCommand>(Label.IsEmpty() ? TEXT("transaction") : Label);
		OpenTransactionId = NextId++;
	}
	return ++TransactionDepth;
}

void FRuntimeCommandHistory::CommitTransaction(FRuntimeCommandContext& Context, int64& OutEntryId, int32& OutCount)
{
	OutCount = OpenTransaction->Num();
	OutEntryId = OpenTransactionId;
	if (!OpenTransaction->IsEmpty())
	{
		TUniquePtr<FRuntimeHistoryEntry> Entry = MakeUnique<FRuntimeHistoryEntry>();
		Entry->Id = OpenTransactionId;
		Entry->Kind = ERuntimeHistoryEntryKind::Command;
		Entry->Label = OpenTransaction->GetLabel();
		Entry->Time = FDateTime::UtcNow();
		Entry->Command = MoveTemp(OpenTransaction);
		Push(MoveTemp(Entry), Context);
	}
	OpenTransaction.Reset();
	OpenTransactionId = INDEX_NONE;
}

bool FRuntimeCommandHistory::EndTransaction(bool bCommit, FRuntimeCommandContext& Context, int64& OutEntryId, int32& OutCommandCount)
{
	OutEntryId = INDEX_NONE;
	OutCommandCount = 0;
	if (TransactionDepth == 0 || !OpenTransaction.IsValid())
	{
		return false;
	}
	if (--TransactionDepth > 0)
	{
		OutCommandCount = OpenTransaction->Num();
		return true; // inner level closed; outer still open
	}

	if (bCommit)
	{
		CommitTransaction(Context, OutEntryId, OutCommandCount);
	}
	else
	{
		OutCommandCount = OpenTransaction->Num();
		OpenTransaction->Undo(Context);
		OpenTransaction->OnDiscarded(Context, true);
		OpenTransaction.Reset();
		OpenTransactionId = INDEX_NONE;
	}
	return true;
}

void FRuntimeCommandHistory::SetLimit(int32 NewLimit, FRuntimeCommandContext& Context)
{
	Limit = FMath::Max(1, NewLimit);
	Trim(Context);
}

void FRuntimeCommandHistory::Clear(FRuntimeCommandContext& Context)
{
	if (OpenTransaction.IsValid())
	{
		int64 Ignored;
		int32 IgnoredCount;
		CommitTransaction(Context, Ignored, IgnoredCount);
	}
	TransactionDepth = 0;
	ClearRedo(Context);
	for (int32 i = UndoStack.Num() - 1; i >= 0; --i)
	{
		if (UndoStack[i]->Command.IsValid())
		{
			UndoStack[i]->Command->OnDiscarded(Context, false);
		}
	}
	UndoStack.Reset();
}

TSharedRef<FJsonObject> FRuntimeCommandHistory::ToJson(int32 MaxEntries, bool bIncludeRedo) const
{
	auto EntryJson = [](const FRuntimeHistoryEntry& E)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetNumberField(TEXT("id"), static_cast<double>(E.Id));
		J->SetStringField(TEXT("label"), E.Label);
		if (E.Kind != ERuntimeHistoryEntryKind::Command)
		{
			J->SetStringField(TEXT("kind"), E.Kind == ERuntimeHistoryEntryKind::Marker ? TEXT("checkpoint") : TEXT("irreversible"));
		}
		return MakeShared<FJsonValueObject>(J);
	};

	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Undo;
	for (int32 i = UndoStack.Num() - 1; i >= 0 && Undo.Num() < MaxEntries; --i)
	{
		Undo.Add(EntryJson(*UndoStack[i]));
	}
	Out->SetArrayField(TEXT("undo"), Undo); // newest first
	Out->SetNumberField(TEXT("undo_count"), UndoStack.Num());
	Out->SetNumberField(TEXT("redo_count"), RedoStack.Num());
	if (bIncludeRedo)
	{
		TArray<TSharedPtr<FJsonValue>> Redo;
		for (int32 i = RedoStack.Num() - 1; i >= 0 && Redo.Num() < MaxEntries; --i)
		{
			Redo.Add(EntryJson(*RedoStack[i]));
		}
		Out->SetArrayField(TEXT("redo"), Redo); // next redo first
	}
	if (OpenTransaction.IsValid())
	{
		TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
		T->SetStringField(TEXT("label"), OpenTransaction->GetLabel());
		T->SetNumberField(TEXT("depth"), TransactionDepth);
		T->SetNumberField(TEXT("commands"), OpenTransaction->Num());
		Out->SetObjectField(TEXT("open_transaction"), T);
	}
	Out->SetNumberField(TEXT("limit"), Limit);
	return Out;
}
