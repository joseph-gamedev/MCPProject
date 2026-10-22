#pragma once

#include "CoreMinimal.h"

/**
 * Remote agents for games on machines nothing can connect to (cloud VMs such as StreamPixel): the game long-polls a
 * small relay (Relay/ - a free Cloudflare Worker) over outbound HTTPS, runs each relayed agent request against the local
 * MCP server and posts the answer back. The game shows a short "AI code"; the user runs Connect.bat with that code once
 * and the coding agent talks to https://<relay>/s/<code>/mcp.
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
