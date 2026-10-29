#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RuntimeToolsetsMCPSettings.generated.h"

UENUM()
enum class ERuntimeToolsetsMCPExposure : uint8
{
	/** Only session_* tools are listed; agents discover the rest with session_describe_toolset and run them with session_call_tool. */
	Meta,
	/** Every enabled tool is listed. */
	Eager,
};

/** Settings for the optional MCP adapter (local testing over Epic's ModelContextProtocol server). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Runtime Toolsets MCP Adapter"))
class URuntimeToolsetsMCPSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Start the MCP HTTP server on startup if nothing else started it. */
	UPROPERTY(Config, EditAnywhere, Category = "Server")
	bool bAutoStart = true;

	UPROPERTY(Config, EditAnywhere, Category = "Server")
	int32 Port = 8018;

	UPROPERTY(Config, EditAnywhere, Category = "Server")
	FString Path = TEXT("/mcp");

	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	ERuntimeToolsetsMCPExposure ExposureMode = ERuntimeToolsetsMCPExposure::Meta;

	/** Prepended to MCP tool names (e.g. "rt_"). Tool names use underscores: actors_spawn_actor. */
	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	FString ToolNamePrefix;

	/** Session id given to every MCP client (Epic's server does not expose client sessions to tools). */
	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	FString SessionId = TEXT("mcp-local");

	/**
	 * Relay for remote agents (see Relay/ and McpPublicAccess.h), e.g. "https://unreal-mcp-relay.you.workers.dev".
	 * Empty = local MCP only. -RuntimeToolsetsRelay=<url> overrides; -NoRuntimeToolsetsRelay turns it off.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Remote agents")
	FString RelayUrl;

	/** Token the game presents to the relay (the relay's HOST_TOKEN secret). */
	UPROPERTY(Config, EditAnywhere, Category = "Remote agents")
	FString RelayHostToken;

	/** Packaged builds connect to the relay automatically (editor sessions need -RuntimeToolsetsRelay). */
	UPROPERTY(Config, EditAnywhere, Category = "Remote agents")
	bool bRelayInPackagedBuilds = true;

	/** Show the AI code and the connection state in a corner of the screen. */
	UPROPERTY(Config, EditAnywhere, Category = "Remote agents")
	bool bShowConnectOverlay = true;
};
