#include "RuntimeToolTypes.h"
#include "RuntimeJson.h"

const TCHAR* LexToString(ERuntimeToolError Error)
{
	switch (Error)
	{
	case ERuntimeToolError::None: return TEXT("none");
	case ERuntimeToolError::InvalidArguments: return TEXT("invalid_arguments");
	case ERuntimeToolError::UnknownTool: return TEXT("unknown_tool");
	case ERuntimeToolError::ToolsetDisabled: return TEXT("toolset_disabled");
	case ERuntimeToolError::NotFound: return TEXT("not_found");
	case ERuntimeToolError::AmbiguousTarget: return TEXT("ambiguous_target");
	case ERuntimeToolError::NotAllowed: return TEXT("not_allowed");
	case ERuntimeToolError::NoWorld: return TEXT("no_world");
	case ERuntimeToolError::WrongState: return TEXT("wrong_state");
	case ERuntimeToolError::ConfirmRequired: return TEXT("confirm_required");
	case ERuntimeToolError::NothingToUndo: return TEXT("nothing_to_undo");
	case ERuntimeToolError::Unsupported: return TEXT("unsupported");
	case ERuntimeToolError::Busy: return TEXT("busy");
	case ERuntimeToolError::Timeout: return TEXT("timeout");
	case ERuntimeToolError::Cancelled: return TEXT("cancelled");
	case ERuntimeToolError::Internal: return TEXT("internal");
	}
	return TEXT("internal");
}

TSharedRef<FJsonObject> FToolResult::ToJson() const
{
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"), IsOk());
	if (!IsOk())
	{
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("code"), LexToString(Error));
		Err->SetStringField(TEXT("message"), Message);
		if (!Hint.IsEmpty())
		{
			Err->SetStringField(TEXT("hint"), Hint);
		}
		Out->SetObjectField(TEXT("error"), Err);
		return Out;
	}

	if (Payload.IsValid())
	{
		for (const FString& Key : RuntimeJson::Keys(Payload))
		{
			if (Key != TEXT("ok"))
			{
				Out->SetField(Key, Payload->TryGetField(Key));
			}
		}
	}
	if (HistoryEntryId != INDEX_NONE)
	{
		Out->SetNumberField(TEXT("history_id"), static_cast<double>(HistoryEntryId));
	}
	if (Attachments.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FToolAttachment& A : Attachments)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("mime"), A.MimeType);
			Item->SetNumberField(TEXT("w"), A.Width);
			Item->SetNumberField(TEXT("h"), A.Height);
			Item->SetNumberField(TEXT("bytes"), static_cast<double>(A.Bytes.Num()));
			Items.Add(MakeShared<FJsonValueObject>(Item));
		}
		Out->SetArrayField(TEXT("attachments"), Items);
	}
	return Out;
}

FString FToolResult::ToJsonString() const
{
	return RuntimeJson::Write(ToJson());
}

FToolResult FToolResult::Ok(TSharedPtr<FJsonObject> InPayload)
{
	FToolResult R;
	R.Payload = InPayload;
	return R;
}

FToolResult FToolResult::Fail(ERuntimeToolError InError, const FString& InMessage, const FString& InHint)
{
	FToolResult R;
	R.Error = InError == ERuntimeToolError::None ? ERuntimeToolError::Internal : InError;
	R.Message = InMessage;
	R.Hint = InHint;
	return R;
}
