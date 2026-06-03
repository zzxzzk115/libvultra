# Harness Config

Single source of truth for the engine AI harness. When a value here changes, update
this file first, then any docs or clients that mirror it.

## Endpoints and build target

- Runtime MCP host: `127.0.0.1` (localhost only in v1; other hosts are coerced to it).
- Runtime MCP default port: `8848`. Override with `--mcp-port <n>`.
- MCP endpoint: `http://127.0.0.1:<port>/mcp` (JSON-RPC over HTTP, `POST /mcp`).
- MJPEG stream endpoint (when started): `http://127.0.0.1:<port>/stream/<id>`.
- Primary build target: `vultra-app` (`xmake build -y vultra-app`).
- Python client: `tools/python/vultra_client` (point it at the same host/port).

## Start commands

```
xmake run vultra-app --editor --mcp --project example.vproject --no-xr
xmake run vultra-app --editor --mcp --mcp-port 8867 --project example.vproject --no-xr
```

## Tool argument casing

- Wire keys use **snake_case** (e.g. `component_kind`, `entity_kind`), matching the
  editor command names (`scene.add_component`, `scene.update_component`).
- Published `inputSchema`s advertise only the canonical snake_case key.
- For backward compatibility the editor commands still tolerate `componentKind` and a
  bare `kind` as aliases, but new tools, clients, and docs should use snake_case.

## Fresh-agent decision tree

1. Read `AGENTS.md`, `ai/README.md`, this file, and the relevant `ai/tasks/` file.
2. Is MCP available? Send `initialize` then `tools/call -> vultra.runtime.status`.
   - Yes → use MCP for live editor/runtime work and verification.
   - No → degrade gracefully: read the `ai/` files and source directly; verify with
     `xmake build -y vultra-app` and targeted reasoning instead of live calls.
3. Pick work from `ai/tasks/`. Priority order when unspecified:
   - explicit P0/P1/P2 items in an active audit (e.g. the codex-debt roadmap) first;
   - then the "Remaining Work Queue" / "Next steps" of the in-flight task;
   - then opportunistic cleanups that reduce entropy.
4. If a task is blocked, record the blocker in an `ai/workspace/` note (using
   `TEMPLATE.md`) and move to the next unblocked item rather than stalling.

## Canonical smoke command set

The smallest set that proves a Runtime MCP change end to end:

1. `initialize`
2. `tools/list`
3. `tools/call -> vultra.runtime.status`
4. a representative `scene.*` tool (e.g. `vultra.scene.component_metadata`)
5. when rendering is involved: `vultra.render.capture_rgb` then inspect the PNG

Write all smoke scratch under `build/.tmp/` and reclaim it with
`tools/clean-mcp-tmp.ps1` / `tools/clean-mcp-tmp.sh` (see `AGENTS.md`).
