#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWorld;
class FRuntimeSceneGraph;

/** Everything a command needs to apply itself. */
struct FRuntimeCommandContext
{
	UWorld* World = nullptr;
	FRuntimeSceneGraph* Graph = nullptr;
	FString SessionId;
	/** True while the world is being torn down: commands must not spawn or destroy actors. */
	bool bWorldTearingDown = false;
};

/** A reversible change. Mutating tools wrap their work in a command and run it through the session history. */
class RUNTIMETOOLSETSCORE_API IRuntimeCommand
{
public:
	virtual ~IRuntimeCommand() = default;

	/** Short human label, e.g. "spawn cube n12". */
	virtual FString GetLabel() const = 0;

	/** Apply the change for the first time. Must leave state untouched when returning false. */
	virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) = 0;

	/** Revert the change. Returns false if the target vanished (entry is reported as stale). */
	virtual bool Undo(FRuntimeCommandContext& Context) = 0;

	/** Re-apply after Undo. Default calls Do. */
	virtual bool Redo(FRuntimeCommandContext& Context)
	{
		FString Ignored;
		return Do(Context, Ignored);
	}

	/** Compact description of what changed (for the tool result). */
	virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const { return nullptr; }

	/**
	 * Called when the entry leaves history for good (trimmed, cleared, or redo stack dropped).
	 * @param bWasUndone true when the command is currently undone (it sat on the redo stack).
	 */
	virtual void OnDiscarded(FRuntimeCommandContext& Context, bool bWasUndone) {}
};

/** Runs several commands as one entry. Undo runs in reverse order. */
class RUNTIMETOOLSETSCORE_API FCompositeCommand : public IRuntimeCommand
{
public:
	explicit FCompositeCommand(const FString& InLabel) : Label(InLabel) {}
	UE_NONCOPYABLE(FCompositeCommand);

	void Add(TUniquePtr<IRuntimeCommand> Command) { Commands.Add(MoveTemp(Command)); }
	/** Adds a command that has already been applied (used by open transactions). */
	void AddApplied(TUniquePtr<IRuntimeCommand> Command) { Commands.Add(MoveTemp(Command)); }
	int32 Num() const { return Commands.Num(); }
	bool IsEmpty() const { return Commands.IsEmpty(); }
	void SetLabel(const FString& InLabel) { Label = InLabel; }

	virtual FString GetLabel() const override { return Label; }
	virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override;
	virtual bool Undo(FRuntimeCommandContext& Context) override;
	virtual bool Redo(FRuntimeCommandContext& Context) override;
	virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override;
	virtual void OnDiscarded(FRuntimeCommandContext& Context, bool bWasUndone) override;

private:
	FString Label;
	TArray<TUniquePtr<IRuntimeCommand>> Commands;
};

/** Command built from lambdas, for simple value changes (cvars, names, tags...). */
class RUNTIMETOOLSETSCORE_API FLambdaCommand : public IRuntimeCommand
{
public:
	using FDoFn = TFunction<bool(FRuntimeCommandContext&, FString&)>;
	using FUndoFn = TFunction<bool(FRuntimeCommandContext&)>;

	FLambdaCommand(const FString& InLabel, FDoFn InDo, FUndoFn InUndo, TSharedPtr<FJsonObject> InDelta = nullptr)
		: Label(InLabel), DoFn(MoveTemp(InDo)), UndoFn(MoveTemp(InUndo)), Delta(InDelta)
	{
	}

	virtual FString GetLabel() const override { return Label; }
	virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override { return DoFn(Context, OutError); }
	virtual bool Undo(FRuntimeCommandContext& Context) override { return UndoFn(Context); }
	virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override { return Delta; }

private:
	FString Label;
	FDoFn DoFn;
	FUndoFn UndoFn;
	TSharedPtr<FJsonObject> Delta;
};
