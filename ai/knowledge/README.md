# Engine Knowledge

Use this directory for stable facts about Vultra that agents should know before
planning or editing.

Knowledge files should be concise and factual. If the behavior changes, update
the relevant knowledge file in the same task.

## Files

- `harness-config.md`: single source for MCP host/port, build target, tool-arg
  casing, the fresh-agent decision tree, and the canonical smoke command set.
- `cpp-conventions.md`: assert-vs-error-handling, RAII/ownership, and god-file rules.
- `ai-runtime-rpc.md`: Runtime MCP/RPC facts (implemented today vs planned direction).
- `packaged-runtime.md`: packaged runtime (VPK) discovery and mounting rules.
- `project-workspaces.md`: project-local AI workspace layout and boundaries.
- `lua-scripting.md`: stable gameplay Lua binding rules and pitfalls. The
  user-facing scripting guide is `doc/lua_scripting.md`.
- `vultra-formats.md`: stable Vultra file formats and extensions.
- `vscode-workspace.md`: VS Code clangd and xmake workspace defaults.
