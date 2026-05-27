# Vultra MCP Server

`vultra_mcp.py` is a small stdio MCP server for Vultra's dual-layer AI Harness.
It exposes engine and project context as resources and provides dry-run-first
tools for controlled Harness and project-content edits.

## Run

```powershell
python tools/vultra_mcp/vultra_mcp.py --engine-root .
```

For an external game project:

```powershell
python tools/vultra_mcp/vultra_mcp.py --engine-root C:\path\to\libvultra --project C:\path\to\my_game
```

## Client Setup

MCP servers are started by the AI client. Register this server in the client
configuration so the client can launch it automatically and connect over stdio.

Example config files:

- `examples/mcp.json`: generic repo-local setup.
- `examples/claude_desktop_config.json`: absolute-path desktop setup.
- `examples/project.mcp.json`: external Vultra game project setup.

After connecting, call:

```text
vultra.bootstrap_context
```

## Safety Model

- Tool calls default to `dry_run`.
- `mode: "write"` requires `expected_sha256`.
- Engine writes are limited to `ai/**`.
- Project writes are limited to project `ai/**`, `resources/**/*.vscn`,
  `resources/render/**`, `resources/shaders/**`, and `scripts/**`.
- Project tools do not edit engine C++ source.
