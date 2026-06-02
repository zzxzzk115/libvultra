# Vultra Agent Entry

Before planning or editing in this repository, read:

- `ai/README.md`
- `ai/specs/ai-harness.md`
- `ai/skills/README.md`
- relevant files under `ai/knowledge/`
- relevant task files under `ai/tasks/`, if they exist

If the client supports MCP for live editor/runtime testing, use the C++ Runtime
MCP built into `vultra-app`:

- Start: `xmake run vultra-app --editor --mcp --project example.vproject --no-xr`
- Endpoint: `http://127.0.0.1:8848/mcp`
- Protocol: MCP-over-HTTP JSON-RPC request/response, `POST /mcp`
- First smoke calls: `initialize`, `tools/list`,
  `tools/call` -> `vultra.runtime.status`

Use Runtime MCP for playback, profiler, scene/main-camera, render graph, frame resource, full-resolution texture dump, pipeline reload, and RenderDoc capture diagnostics. For repository AI context, read the `ai/` files listed above directly before planning or editing.

For game project work, also read the project-local:

- `ai/README.md`
- `ai/game.md`
- relevant `ai/specs/`
- relevant `ai/tasks/`
- relevant `ai/knowledge/`

## Gameplay API Parity

When adding or changing engine features that can affect gameplay authors:

- Decide whether the feature needs a player-facing Lua binding.
- Check existing features in the same subsystem for missing bindings.
- If a binding is missing because the runtime service or data model is incomplete,
  implement the service/data layer first, then add the Lua binding on top of it.
- Keep Lua bindings thin: call services/components instead of duplicating engine
  behavior in binding code.
- Update `doc/lua_scripting.md`, repository AI docs under `ai/`, and any
  affected project-local `ai/` docs.
- For project work, record new script assumptions in the project `ai/game.md`,
  `ai/specs/`, or `ai/knowledge/` before relying on them in generated content.

## Rules

- Keep the engine Harness and project Harness separate.
- Project agents must not directly edit Vultra engine source.
- Prefer dry-run proposals before project content writes.
- Record verification results and handoff notes in `ai/workspace/`.
- Promote repeated stable facts to `ai/knowledge/`.
- Promote repeated workflows to skills.
- Preserve unrelated user changes in the worktree.
- Do not assume MCP is available unless the client has registered and started
  the `vultra` server. If MCP is unavailable, follow the same rules by reading
  the files directly.

## Verification

Use the smallest command set that proves the change. Common checks include:

- `xmake build -y vultra-app`
- `xmake build -y <target>`
- asset import or pack scripts when resources change
- Runtime MCP smoke tests when editor automation or runtime diagnostics change
