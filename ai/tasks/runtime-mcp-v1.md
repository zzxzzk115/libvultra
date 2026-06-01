# Runtime MCP V1

## Goal

Add a C++ localhost MCP server to `vultra-app` for live editor/runtime
diagnostics and automation.

## Scope

- Start the server from AI Agent editor settings.
- Start the server from CLI `--mcp`, with optional `--mcp-host` and
  `--mcp-port`.
- Bind only to localhost.
- Expose playback, profiler, frame graph, frame resource, pipeline reload, and
  frame capture tools.
- Repository/project AI Harness context is read from tracked `ai/` files; no
  external MCP server is kept.

## Verification

- `xmake build -y vultra-app`
- Manual HTTP MCP smoke test against `POST /mcp`.
