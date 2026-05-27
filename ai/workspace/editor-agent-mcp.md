# Editor Agent MCP

## Current Slice

Added the first editor-facing configuration layer for future MCP-backed agent
features.

## Notes

- Editor Settings owns user preferences only.
- Long-running MCP startup and agent connections should be handled by a service,
  not by the settings window.
- AI Auto Layout should eventually use graph JSON plus editor screenshots to
  produce and verify layout patches.

## Verification

- Current: `xmake build -y vultra-app` passed.
