#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "RuntimeToolTypes.h"
#include "UObject/Object.h"
#include "RuntimeToolCall.generated.h"

class AActor;
class FRuntimeSceneGraph;
class FToolInvocation;
class IRuntimeCommand;
class UWorld;
struct FRuntimeSceneNode;

/**
 * Free-form JSON argument or result (any JSON value). Use it where Epic's toolsets take a "JsonParams" string:
 * the agent passes real JSON instead of JSON inside a string.
 */
USTRUCT(BlueprintType)
struct RUNTIMETOOLSETSCORE_API FRuntimeToolJson
{
	GENERATED_BODY()

	TSharedPtr<FJsonValue> Value;

	bool IsSet() const { return Value.IsValid() && !Value->IsNull(); }
	TSharedPtr<FJsonObject> AsObject() const;
	static FRuntimeToolJson Make(const TSharedPtr<FJsonValue>& InValue);
	static FRuntimeToolJson Make(const TSharedPtr<FJsonObject>& InObject);
};

/** The running tool call, for code inside reflected (UFUNCTION) tools. Game thread only. */
namespace RuntimeToolCall
{
	/** The invocation being executed (null outside a tool call). */
	RUNTIMETOOLSETSCORE_API FToolInvocation* Current();
	RUNTIMETOOLSETSCORE_API UWorld* GetWorld();
	RUNTIMETOOLSETSCORE_API FRuntimeSceneGraph* GetGraph();

	/** Fails the call (Epic: RaiseScriptError). Return any default value afterwards; it is ignored. */
	RUNTIMETOOLSETSCORE_API void Fail(ERuntimeToolError Error, const FString& Message, const FString& Hint = FString());
	RUNTIMETOOLSETSCORE_API bool HasFailed();

	/** Runs a command through the caller's undo history (tools marked Undoable). Fails the call on error. */
	RUNTIMETOOLSETSCORE_API bool RunCommand(TUniquePtr<IRuntimeCommand> Command);

	/** Resolves a reference string (node id, path, actor:Name, object path); fails the call if not found. */
	RUNTIMETOOLSETSCORE_API UObject* Resolve(const FString& Reference, UClass* Expected = nullptr);

	/** The scene graph node of an actor (null if the actor was not created by tools). */
	RUNTIMETOOLSETSCORE_API const FRuntimeSceneNode* NodeOf(const AActor* Actor);
	/** Short reference for results: node id, else actor:Name, else object path. */
	RUNTIMETOOLSETSCORE_API FString MakeReference(const UObject* Object);

	/** Lets an async tool run longer than the default timeout. */
	RUNTIMETOOLSETSCORE_API void SetTimeout(double Seconds);
	RUNTIMETOOLSETSCORE_API void ReportProgress(float Fraction, const FString& Message);
}

/**
 * Pending result of an asynchronous tool (Epic: UToolCallAsyncResult). Subclasses declare a UPROPERTY named Value and a
 * SetValue that assigns it and calls Complete(). The call finishes when SetValue or SetError runs (game thread).
 */
UCLASS(Abstract, BlueprintType, MinimalAPI)
class URuntimeToolAsyncResult : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "RuntimeToolsets")
	bool bIsComplete = false;

	UPROPERTY(BlueprintReadOnly, Category = "RuntimeToolsets")
	FString Error;

	/** Completes the call with an error. */
	RUNTIMETOOLSETSCORE_API bool SetError(const FString& InError, ERuntimeToolError Code = ERuntimeToolError::Internal);

	/** Called by the dispatcher: completes the invocation when the result completes (now, if it already has). */
	RUNTIMETOOLSETSCORE_API void Bind(const TSharedRef<FToolInvocation>& Invocation);

	/** Fires once if the call is cancelled or times out. */
	TFunction<void()> OnCancelled;

protected:
	/** Completes the call with the Value property. */
	RUNTIMETOOLSETSCORE_API bool Complete();

private:
	void Finish();
	TSharedPtr<FToolInvocation> Invocation;
	ERuntimeToolError ErrorCode = ERuntimeToolError::Internal;
};

/** Async result with no value. */
UCLASS(BlueprintType)
class RUNTIMETOOLSETSCORE_API URuntimeToolAsyncResultVoid : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue() { return Complete(); }
};

/** Async result carrying free-form JSON. */
UCLASS(BlueprintType)
class RUNTIMETOOLSETSCORE_API URuntimeToolAsyncResultJson : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FRuntimeToolJson& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY(BlueprintReadOnly, Category = "RuntimeToolsets")
	FRuntimeToolJson Value;
};
