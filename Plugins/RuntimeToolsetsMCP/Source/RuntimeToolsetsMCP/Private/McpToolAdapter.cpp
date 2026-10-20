#include "McpToolAdapter.h"
#include "ModelContextProtocolSession.h"
#include "ModelContextProtocolToolResults.h"
#include "RuntimeJson.h"
#include "RuntimeToolsetsCore.h"
#include "Misc/Base64.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeToolsetsMCPAdapter, Log, All);

namespace
{
	/** Builds an MCP tool result: compact JSON text plus image content. No structuredContent, to avoid sending everything twice. */
	FModelContextProtocolToolResult ToMcpResult(const FToolResult& Result)
	{
		TArray<TSharedPtr<FJsonValue>> Content;

		TSharedRef<FJsonObject> Text = MakeShared<FJsonObject>();
		Text->SetStringField(TEXT("type"), TEXT("text"));
		Text->SetStringField(TEXT("text"), Result.ToJsonString());
		Content.Add(MakeShared<FJsonValueObject>(Text));

		for (const FToolAttachment& A : Result.Attachments)
		{
			if (!A.MimeType.StartsWith(TEXT("image/")))
			{
				continue;
			}
			TSharedRef<FJsonObject> Image = MakeShared<FJsonObject>();
			Image->SetStringField(TEXT("type"), TEXT("image"));
			Image->SetStringField(TEXT("mimeType"), A.MimeType);
			Image->SetStringField(TEXT("data"), FBase64::Encode(A.Bytes.GetData(), static_cast<uint32>(A.Bytes.Num())));
			Content.Add(MakeShared<FJsonValueObject>(Image));
		}

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetArrayField(TEXT("content"), Content);
		if (!Result.IsOk())
		{
			Out->SetBoolField(TEXT("isError"), true);
		}
		return FModelContextProtocolToolResult(Out);
	}
}

FMcpToolAdapter::FMcpToolAdapter(const TSharedRef<const FRuntimeToolDescriptor>& InTool, const FString& InMcpName, const FString& InSessionId)
	: Tool(InTool), McpName(InMcpName), SessionId(InSessionId)
{
}

FString FMcpToolAdapter::MakeMcpName(const FString& QualifiedName, const FString& Prefix)
{
	return Prefix + QualifiedName.Replace(TEXT("."), TEXT("_"));
}

FString FMcpToolAdapter::GetDescription() const
{
	FString Description = Tool->Description;
	if (Tool->bMutating && !Tool->bUndoable)
	{
		Description += TEXT(" (not undoable)");
	}
	return Description;
}

TSharedPtr<FJsonObject> FMcpToolAdapter::GetInputJsonSchema() const
{
	return Tool->InputSchema;
}

FString FMcpToolAdapter::RequestKey(const FModelContextProtocolToolRequestId& RequestId)
{
	return RequestId.IsValid() ? RuntimeJson::WriteValue(RequestId.RequestId) : FString();
}

void FMcpToolAdapter::RunAsync(const FModelContextProtocolToolRequestId& RequestId, const TSharedPtr<FJsonObject>& Params, const FResultCallback& OnComplete)
{
	if (!IRuntimeToolsetsCore::IsAvailable())
	{
		OnComplete(UE::ModelContextProtocol::MakeErrorResult(TEXT("RuntimeToolsetsCore is not loaded")));
		return;
	}
	FToolCallerContext Caller;
	Caller.SessionId = SessionId;
	Caller.ClientName = TEXT("mcp");

	const FString Key = RequestKey(RequestId);
	TWeakPtr<FMcpToolAdapter> WeakThis = StaticCastSharedRef<FMcpToolAdapter>(AsShared());
	FResultCallback Callback = OnComplete;

	TSharedRef<IToolCallHandle> Handle = IRuntimeToolsetsCore::Get().Invoke(Tool->QualifiedName, Params, Caller,
		FOnToolComplete::CreateLambda([WeakThis, Key, Callback](const FToolResult& Result)
		{
			if (TSharedPtr<FMcpToolAdapter> This = WeakThis.Pin())
			{
				FScopeLock Lock(&This->PendingLock);
				This->Pending.Remove(Key);
			}
			Callback(ToMcpResult(Result));
		}),
		FOnToolProgress::CreateLambda([Name = Tool->QualifiedName](float Fraction, const FString& Message)
		{
			// Epic's server exposes no per-tool progress API; it sends its own interval heartbeats.
			UE_LOG(LogRuntimeToolsetsMCPAdapter, Verbose, TEXT("%s progress %.0f%% %s"), *Name, Fraction * 100.f, *Message);
		}));

	if (!Key.IsEmpty() && !Handle->IsDone())
	{
		FScopeLock Lock(&PendingLock);
		Pending.Add(Key, Handle);
	}
}

void FMcpToolAdapter::CancelAsync(const FModelContextProtocolToolRequestId& RequestId)
{
	TSharedPtr<IToolCallHandle> Handle;
	{
		FScopeLock Lock(&PendingLock);
		Pending.RemoveAndCopyValue(RequestKey(RequestId), Handle);
	}
	if (Handle.IsValid())
	{
		Handle->Cancel();
	}
}
