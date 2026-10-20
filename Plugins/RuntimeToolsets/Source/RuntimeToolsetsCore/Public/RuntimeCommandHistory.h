#pragma once

#include "CoreMinimal.h"
#include "RuntimeCommand.h"

enum class ERuntimeHistoryEntryKind : uint8
{
	Command,
	/** Named checkpoint. */
	Marker,
	/** An irreversible tool ran here (console command...). Undo skips it and reports it. */
	Irreversible,
};

struct FRuntimeHistoryEntry
{
	int64 Id = 0;
	ERuntimeHistoryEntryKind Kind = ERuntimeHistoryEntryKind::Command;
	FString Label;
	FDateTime Time;
	TUniquePtr<IRuntimeCommand> Command;
};

/** Result of an undo/redo pass. */
struct FRuntimeHistoryStepResult
{
	int32 Applied = 0;
	TArray<TSharedPtr<FJsonObject>> Deltas;
	TArray<FString> SkippedIrreversible;
	TArray<FString> Stale;
	TArray<FString> PassedMarkers;
};

/** Per-session undo/redo history with transactions and checkpoints. Game thread only. */
class RUNTIMETOOLSETSCORE_API FRuntimeCommandHistory
{
public:
	explicit FRuntimeCommandHistory(int32 InLimit = 200) : Limit(FMath::Max(1, InLimit)) {}
	~FRuntimeCommandHistory();

	FRuntimeCommandHistory(const FRuntimeCommandHistory&) = delete;
	FRuntimeCommandHistory& operator=(const FRuntimeCommandHistory&) = delete;

	/**
	 * Applies the command and records it (or appends it to the open transaction).
	 * @return Entry id (the transaction's id when one is open), or INDEX_NONE on failure.
	 */
	int64 Execute(TUniquePtr<IRuntimeCommand> Command, FRuntimeCommandContext& Context, FString& OutError, TSharedPtr<FJsonObject>& OutDelta);

	/** Records that an irreversible tool ran. Commits any open transaction first. */
	void RecordIrreversible(const FString& Label, FRuntimeCommandContext& Context);

	FRuntimeHistoryStepResult Undo(FRuntimeCommandContext& Context, int32 Count);
	/** Undoes until the named marker is on top of the stack. Returns false if the marker is unknown. */
	bool UndoToMarker(FRuntimeCommandContext& Context, const FString& Marker, FRuntimeHistoryStepResult& OutResult);
	FRuntimeHistoryStepResult Redo(FRuntimeCommandContext& Context, int32 Count);

	int64 Checkpoint(const FString& Name, FRuntimeCommandContext& Context);
	bool HasMarker(const FString& Name) const;

	/** Opens (or nests) a transaction. Returns the depth after opening. */
	int32 BeginTransaction(const FString& Label);
	/**
	 * Closes one nesting level. When the outermost level closes: commit pushes one entry, rollback undoes everything inside.
	 * @return false when no transaction is open.
	 */
	bool EndTransaction(bool bCommit, FRuntimeCommandContext& Context, int64& OutEntryId, int32& OutCommandCount);
	bool IsInTransaction() const { return TransactionDepth > 0; }

	void SetLimit(int32 NewLimit, FRuntimeCommandContext& Context);
	int32 GetLimit() const { return Limit; }

	int32 NumUndo() const { return UndoStack.Num(); }
	int32 NumRedo() const { return RedoStack.Num(); }

	/** Discards everything. Commands get OnDiscarded so soft-deleted actors are destroyed (unless tearing down). */
	void Clear(FRuntimeCommandContext& Context);

	TSharedRef<FJsonObject> ToJson(int32 MaxEntries, bool bIncludeRedo) const;

private:
	void Push(TUniquePtr<FRuntimeHistoryEntry> Entry, FRuntimeCommandContext& Context);
	bool UndoOne(FRuntimeCommandContext& Context, FRuntimeHistoryStepResult& Result);
	void ClearRedo(FRuntimeCommandContext& Context);
	void Trim(FRuntimeCommandContext& Context);
	void CommitTransaction(FRuntimeCommandContext& Context, int64& OutEntryId, int32& OutCount);

	TArray<TUniquePtr<FRuntimeHistoryEntry>> UndoStack;
	TArray<TUniquePtr<FRuntimeHistoryEntry>> RedoStack;
	TUniquePtr<FCompositeCommand> OpenTransaction;
	int64 OpenTransactionId = INDEX_NONE;
	int32 TransactionDepth = 0;
	int32 Limit;
	int64 NextId = 1;
};
