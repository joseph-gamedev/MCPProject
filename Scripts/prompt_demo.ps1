# Drives the running game from a plain-English prompt: a separate Claude Code session gets only the
# game's MCP server (no scripts, no tool names) and does the work itself.
# Usage: start the game (or PIE) first, then:  powershell -File Scripts/prompt_demo.ps1 "your request"
param([string]$Prompt = "You are connected to a running Unreal Engine game through the unreal-runtime MCP server. Create 2 different famous buildings in the scene - the Eiffel Tower and the Great Pyramid of Giza - placed side by side, and add a HUD label on top of each building showing its name. Then point the camera so both buildings and their labels are visible and take a screenshot to check the result.")
$config = Join-Path $env:TEMP "unreal-runtime-mcp.json"
'{"mcpServers":{"unreal-runtime":{"type":"http","url":"http://127.0.0.1:8018/mcp"}}}' | Out-File -Encoding ascii $config
claude -p $Prompt --mcp-config $config --strict-mcp-config --allowedTools "mcp__unreal-runtime"
