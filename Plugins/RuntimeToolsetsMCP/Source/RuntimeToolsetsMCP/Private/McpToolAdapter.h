#pragma once

#include "CoreMinimal.h"
#include "IModelContextProtocolTool.h"
#include "RuntimeToolset.h"

class IToolCallHandle;

/** Exposes one RuntimeToolsets tool as an Epic MCP tool. All Epic MCP API usage is confined to this file pair. */
class FMcpToolAdapter final : public IModelContextProtocolTool
{
public:
	FMcpToolAdapter(const TSharedRef<const FRuntimeToolDescriptor>& InTool, const FString& InMcpName, const FString& InSessionId);

	virtual FString GetName() const override { return McpName; }
	virtual FString GetDescription() const override;
	virtual TSharedPtr<FJsonObject> GetInputJsonSchema() const override;
	virtual void RunAsync(const FModelContextProtocolToolRequestId& RequestId, const TSharedPtr<FJsonObject>& Params, const FResultCallback& OnComplete) override;
	virtual void CancelAsync(const FModelContextProtocolToolRequestId& RequestId) override;

	/** "actors.spawn_actor" -> Prefix + "actors_spawn_actor" */
	static FString MakeMcpName(const FString& QualifiedName, const FString& Prefix);

private:
	static FString RequestKey(const FModelContextProtocolToolRequestId& RequestId);

	TSharedRef<const FRuntimeToolDescriptor> Tool;
	FString McpName;
	FString SessionId;

	FCriticalSection PendingLock;
	TMap<FString, TSharedPtr<IToolCallHandle>> Pending;
};
