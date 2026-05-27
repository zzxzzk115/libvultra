# Vultra Agent Entry

Before planning or editing in this repository, read:

- `ai/README.md`
- `ai/specs/ai-harness.md`
- `ai/skills/README.md`
- relevant files under `ai/knowledge/`
- relevant task files under `ai/tasks/`, if they exist

If the client supports MCP, connect the Vultra MCP server before planning:

- Server: `vultra`
- Command: `python tools/vultra_mcp/vultra_mcp.py --engine-root .`
- Working directory: repository root
- First tool call: `vultra.bootstrap_context`

Client config examples live under `tools/vultra_mcp/examples/`.

For game project work, also read the project-local:

- `ai/README.md`
- `ai/game.md`
- relevant `ai/specs/`
- relevant `ai/tasks/`

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
- MCP smoke tests when `tools/vultra_mcp/` changes
