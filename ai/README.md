# Vultra AI Harness

This directory is the tracked AI collaboration layer for Vultra engine development.
It is separate from `.vultra/`, which remains local runtime/editor state.

## Layers

- `specs/` stores durable engine specs and design decisions.
- `tasks/` stores task-centered execution plans with scope and acceptance criteria.
- `workspace/` stores journals, handoff notes, verification logs, and proposed patches.
- `knowledge/` stores stable project facts that agents should reuse across sessions.
- `agents/` defines agent roles and handoff rules.
- `skills/` stores repository skills and their agent metadata.

## Workflow

1. Pick or write a spec before implementation starts.
2. Split work into one focused task under `tasks/`.
3. Select a relevant skill from `skills/README.md`.
4. For gameplay-facing features, check Lua binding parity before coding:
   decide whether players need access, audit nearby missing bindings, implement
   missing service/data support first, then bind the supported surface.
5. Implement in a clean context and keep unrelated user changes intact.
6. Update player-facing docs and AI knowledge when behavior or scripting APIs
   change.
7. Verify with the smallest command set that proves the change.
8. Record results and next handoff notes under `workspace/`.

Project-level game creation uses the same shape inside each Vultra game project,
but project knowledge must stay in the project workspace.

## Gameplay Scripting Parity

Engine features are not complete for game authors until the player-facing
surface is considered. For every new gameplay-relevant system, component,
service, or editor command:

- expose stable, useful operations to Lua when players reasonably need them;
- audit existing same-subsystem features for missing Lua bindings;
- avoid papering over missing services in binding code; add the runtime service
  or data model first, then bind it;
- update `doc/lua_scripting.md` and `ai/knowledge/lua-scripting.md`;
- update project-local `ai/` documents when generated games rely on the new
  behavior.

## Runtime MCP

Use the C++ Runtime MCP for live editor/runtime diagnostics and automation:
playback, profiler data, current scene/main camera, render graph source, frame
resources, full-resolution frame texture dumps, pipeline reload, and RenderDoc
capture requests.

Start the editor with Runtime MCP forced on:

```powershell
xmake run vultra-app -- --editor --mcp --project example.vproject --no-xr
```

By default it listens on:

```text
http://127.0.0.1:8848/mcp
```

Use `--mcp-port <port>` to override the port. Send MCP-over-HTTP JSON-RPC
requests to `POST /mcp`. Minimum smoke calls are `initialize`, `tools/list`,
and `tools/call` with `vultra.runtime.status`.

Example tool call body:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "vultra.runtime.status",
    "arguments": {}
  }
}
```

Runtime MCP tools must be treated as live engine operations. Do not use them for
AI Harness file edits. Read repository/project AI context directly from `ai/`
unless the editor is running with Runtime MCP enabled for live automation.
