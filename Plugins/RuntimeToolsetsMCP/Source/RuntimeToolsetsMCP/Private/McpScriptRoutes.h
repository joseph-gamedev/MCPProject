#pragma once

#include "CoreMinimal.h"

/**
 * Plain HTTP for agent scripts (no MCP session, no packages): POST <base>/call {"tool","arguments"} and
 * POST <base>/batch {"calls":[{tool,arguments}...],"atomic"} answer the tool's result JSON ({"ok":true,...}).
 * The relay offers the same two routes at <relay>/s/<code>, so a script is the same for a local or a streamed game.
 * RUNTIMETOOLSETS_SCRIPT_URL holds the base address the agent should use (session_get_build_info reports it).
 */
namespace McpScriptRoutes
{
	void Start(int32 Port, const FString& SessionId);
	void Stop();
	/** The base address scripts use (the relay's when the game is relayed). */
	void SetScriptUrl(const FString& BaseUrl);
}
