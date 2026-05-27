# Editor Agent MCP Task

## Goal

Introduce the first editor-facing configuration layer for future MCP-backed
agent features.

## In Scope

- Add editor settings for agent enablement.
- Add editor settings for MCP server startup preferences.
- Add editor settings for agent endpoint/model.
- Add guardrail settings for project operations, engine operations, and write
  confirmation.
- Persist these settings to `.vultra/editor_settings.json`.
- Document the longer-term editor agent and AI Auto Layout flow.

## Out of Scope

- Starting the MCP process.
- Implementing an MCP client inside the editor.
- Implementing a chat panel.
- Implementing editor operation tools.
- Implementing screenshot capture or AI Auto Layout.

## Relevant Spec

- `ai/specs/editor-agent-mcp.md`

## Implementation Plan

1. Extend `AppState::EditorSettings`.
2. Load and save the new fields in editor settings persistence.
3. Add an `AI Agent` page to Editor Settings.
4. Record the runtime service and AI Auto Layout architecture in the spec.
5. Verify `vultra-app` builds.

## Verification

- Current: `xmake build -y vultra-app` passed.

## Handoff

Next slice should add an editor agent service that reads these settings and owns
MCP process lifecycle and connection state.
