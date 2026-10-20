# 0001: Toolsets-only core with an optional MCP adapter

**Status:** accepted, 2026-10-20

## Context

- Epic's UE 5.8 MCP toolsets depend on editor-only code, so they don't run in a packaged game.
- Epic's `ModelContextProtocol` plugin does run at runtime, but it is NoRedist and Experimental.
- A separate connector plugin will own transport, identity and sessions.

## Decision

- `RuntimeToolsets` has no MCP dependency. It exposes a plain-JSON API (`ListToolsets`, `DescribeToolset`, `Invoke`) that any connector can call.
- A separate plugin, `RuntimeToolsetsMCP`, mirrors the tools into Epic's MCP server for local testing. It can be disabled without touching the core.
- MCP tool names use underscores (`actors_spawn_actor`), because Claude-compatible clients only accept `[A-Za-z0-9_-]`. The core accepts both `actors.spawn_actor` and `actors_spawn_actor`.
- The adapter's default exposure is `Meta`: only `session_*` tools are listed, which keeps `tools/list` small and stable for prompt caching. Everything else is reached through `session_describe_toolset` and `session_call_tool`.

## Consequences

- Epic's server gives tools no MCP session id, so every adapter client shares one session (`mcp-local`). Per-user undo needs the connector.
- Epic's server exposes no per-tool progress API. Progress callbacks reach connectors but not MCP clients.
- The runtime has its own undo system, because the editor transaction system isn't available in packaged games.
