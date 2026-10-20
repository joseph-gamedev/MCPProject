#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Fixed set of error codes returned by every tool. Serialized as snake_case strings. */
enum class ERuntimeToolError : uint8
{
	None,
	InvalidArguments,
	UnknownTool,
	ToolsetDisabled,
	NotFound,
	AmbiguousTarget,
	NotAllowed,
	NoWorld,
	WrongState,
	ConfirmRequired,
	NothingToUndo,
	Unsupported,
	Busy,
	Timeout,
	Cancelled,
	Internal,
};

RUNTIMETOOLSETSCORE_API const TCHAR* LexToString(ERuntimeToolError Error);

/** Who is calling. Each SessionId gets its own undo history. */
struct FToolCallerContext
{
	FString SessionId = TEXT("default");
	FString ClientName;
	/** Overrides the default timeout for deferred (async) tools. */
	TOptional<double> TimeoutSeconds;
};

/** Binary payload attached to a result (e.g. an encoded image). */
struct FToolAttachment
{
	FString MimeType;
	int32 Width = 0;
	int32 Height = 0;
	TArray64<uint8> Bytes;
};

/** Result of one tool call. Payload is a compact delta, never the full world state. */
struct RUNTIMETOOLSETSCORE_API FToolResult
{
	ERuntimeToolError Error = ERuntimeToolError::None;
	FString Message;
	FString Hint;
	TSharedPtr<FJsonObject> Payload;
	TArray<FToolAttachment> Attachments;
	bool bUndoable = false;
	int64 HistoryEntryId = INDEX_NONE;

	bool IsOk() const { return Error == ERuntimeToolError::None; }

	/** {"ok":true,...payload} or {"ok":false,"error":{"code","message","hint"}}. Attachments are described, not embedded. */
	TSharedRef<FJsonObject> ToJson() const;
	FString ToJsonString() const;

	static FToolResult Ok(TSharedPtr<FJsonObject> InPayload = nullptr);
	static FToolResult Fail(ERuntimeToolError InError, const FString& InMessage, const FString& InHint = FString());
};
