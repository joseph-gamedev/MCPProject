#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolTypes.h"
#include "RuntimeToolset.h"
#include "RuntimeCommand.h"
#include "RuntimeObjectResolver.h"

class FRuntimeCommandHistory;
class FRuntimeSceneGraph;
class UWorld;
class AActor;

/**
 * One running tool call, handed to the tool's handler on the game thread.
 *
 * Handlers follow five steps: read (validated) args -> resolve targets -> build a command ->
 * RunCommand -> Succeed(delta). Async tools call Defer() and complete later with Succeed/Fail.
 */
class RUNTIMETOOLSETSCORE_API FToolInvocation : public TSharedFromThis<FToolInvocation>
{
public:
	FToolInvocation(const TSharedRef<const FRuntimeToolDescriptor>& InTool, const TSharedRef<FJsonObject>& InArgs, const FToolCallerContext& InCaller,
		UWorld* InWorld, FRuntimeSceneGraph* InGraph, FRuntimeCommandHistory* InHistory, int32 InDepth);

	const FRuntimeToolDescriptor& GetTool() const { return *Tool; }
	const TSharedRef<FJsonObject>& GetArgs() const { return Args; }
	const FToolCallerContext& GetCaller() const { return Caller; }
	UWorld* GetWorld() const { return World; }
	FRuntimeSceneGraph* GetGraph() const { return Graph; }
	FRuntimeCommandHistory& GetHistory() const { return *History; }
	FRuntimeCommandContext MakeCommandContext() const;
	/** Nesting depth (call_tool). 0 for top-level calls. */
	int32 GetDepth() const { return Depth; }

	// ---- arguments (already validated, defaults filled) ----
	bool Has(const TCHAR* Name) const;
	FString GetString(const TCHAR* Name, const FString& Default = FString()) const;
	int64 GetInt(const TCHAR* Name, int64 Default = 0) const;
	double GetNumber(const TCHAR* Name, double Default = 0.0) const;
	bool GetBool(const TCHAR* Name, bool Default = false) const;
	bool GetVector(const TCHAR* Name, FVector& Out) const;
	bool GetRotator(const TCHAR* Name, FRotator& Out) const;
	TArray<FString> GetStringArray(const TCHAR* Name) const;
	TSharedPtr<FJsonObject> GetObject(const TCHAR* Name) const;
	TSharedPtr<FJsonValue> GetValue(const TCHAR* Name) const;
	/** "limit" clamped to settings. */
	int32 GetLimit() const;
	/** "fields" projection (empty = all). */
	TArray<FString> GetFields() const;

	// ---- target resolution (fails the call and returns false/null on error) ----
	bool ResolveTarget(const TCHAR* ArgName, FResolvedTarget& Out);
	AActor* ResolveActor(const TCHAR* ArgName);

	// ---- outcome ----
	/** Runs a command through the caller's history. On failure the call is failed and false is returned. */
	bool RunCommand(TUniquePtr<IRuntimeCommand> Command, TSharedPtr<FJsonObject>* OutDelta = nullptr);
	/** Marks that an irreversible change happened (ends the undo chain for this session). */
	void RecordIrreversible(const FString& Label);

	void Succeed(TSharedPtr<FJsonObject> Payload = nullptr);
	void Fail(ERuntimeToolError Error, const FString& Message, const FString& Hint = FString());
	void AddAttachment(FToolAttachment&& Attachment);

	/** The handler will complete later (async tool). */
	void Defer();
	bool IsDeferred() const { return bDeferred; }
	bool IsComplete() const { return bComplete; }
	void ReportProgress(float Fraction, const FString& Message);
	bool IsCancelled() const;
	/** Called once if the call is cancelled or times out while deferred. */
	void SetCancelHandler(TFunction<void()> InHandler) { CancelHandler = MoveTemp(InHandler); }

	const FToolResult& GetResult() const { return Result; }

	// ---- wiring (dispatcher only) ----
	TFunction<void(const FToolResult&)> OnFinished;
	TFunction<void(float, const FString&)> OnProgress;
	TFunction<bool()> CancelQuery;
	void NotifyCancelled();

private:
	void Finish();

	TSharedRef<const FRuntimeToolDescriptor> Tool;
	TSharedRef<FJsonObject> Args;
	FToolCallerContext Caller;
	UWorld* World;
	FRuntimeSceneGraph* Graph;
	FRuntimeCommandHistory* History;
	int32 Depth;

	FToolResult Result;
	bool bDeferred = false;
	bool bComplete = false;
	TFunction<void()> CancelHandler;
};
