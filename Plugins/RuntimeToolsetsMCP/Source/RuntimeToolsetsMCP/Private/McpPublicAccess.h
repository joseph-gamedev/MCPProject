#pragma once

#include "CoreMinimal.h"

/**
 * Remote agents for games on machines nothing can connect to (cloud VMs such as StreamPixel): the game long-polls a
 * small relay (Relay/ - a free Cloudflare Worker) over outbound HTTPS, runs each relayed agent request against the local
 * MCP server and posts the answer back. The game shows its AI code and where to connect (<relay>/<code>): that page gives
 * the player one command for their own Claude Code, which then talks to https://<relay>/s/<code>/mcp. A 10-character
 * code is the agent's credential (no shared token); it changes every launch.
 */
namespace McpPublicAccess
{
	/** Whether the relay is used this launch (packaged builds with RelayUrl set, or -RuntimeToolsetsRelay[=url]). */
	bool IsEnabled();
	void Start(int32 Port, const FString& Path);
	void Stop();
	/** The session code shown in the game. */
	FString Code();
}
