# Editor Agent MCP Integration

## Intent

Vultra editor should become an AI-native creative environment where an agent can
inspect editor state, talk with the user, call MCP tools, and perform controlled
editor operations.

This is separate from the repository-level AI Harness. The Harness stores rules
and durable knowledge. The editor agent is a runtime feature inside Vultra.

## Architecture

- Editor Settings stores agent and MCP preferences under `.vultra/`.
- A future agent service owns MCP server startup, client connection state,
  conversation state, and operation dispatch.
- UI windows only present state and user actions. They must not directly own
  long-running MCP or agent processes.
- Engine and project operations must remain guarded by explicit capability
  flags and confirmation rules.

## Runtime Flow

1. Editor loads persisted agent settings.
2. If agent support and auto-start are enabled, the agent service starts the
   configured MCP server command.
3. The agent client connects to MCP and calls the bootstrap context tool.
4. The user talks to the agent in an editor panel.
5. Agent operations are routed through editor tools with guardrails.
6. Results are written to workspace journals or project task notes when useful.

## AI Auto Layout Flow

AI Auto Layout should be a closed-loop editor operation:

1. Capture the graph JSON and current graph viewport screenshot.
2. Provide node dimensions, pin positions, graph type, and current zoom/pan.
3. Let the agent detect overlap, crowded edges, crossings, and excessive gaps.
4. Produce a layout patch that only changes editor graph positions.
5. Apply the patch, refresh the graph, capture again, and verify visually.

## Guardrails

- Project agents must not edit engine source directly.
- Engine operations require a stronger permission than project content
  operations.
- Writes should default to confirmation-required.
- MCP startup command and agent endpoint are user-editable settings, not hard
  coded assumptions.

## Acceptance Criteria

- Editor Settings can store agent enablement, MCP startup command, agent
  endpoint/model, and operation guardrails.
- A future agent service can consume those settings without changing the
  settings file format.
- AI Auto Layout can be implemented as an operation on top of screenshot,
  graph JSON, and editor-position patches.
