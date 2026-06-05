# Editor Agent MCP Integration

## Intent

Vultra editor should become an AI-native creative environment where an agent can
inspect editor state, talk with the user, call MCP tools, and perform controlled
editor operations.

This is separate from the repository-level AI Harness. The Harness stores rules
and durable knowledge. The editor agent is a runtime feature inside Vultra.

## Architecture

- Editor Settings stores agent and MCP preferences under `.vultra/`.
- The agent backend owns the agent process and conversation; the editor's
  `RuntimeMcpServer` owns MCP server startup and connection state.
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

## Implementation (shipped)

The editor `AI Chat` panel (`AiChatWindow`) owns an `IAgentBackend`. v1 ships
`ClaudeCliBackend`, which spawns the Claude CLI headless
(`claude -p --input-format stream-json --output-format stream-json --verbose`),
keeps one persistent process per conversation, and parses stdout NDJSON into
events on a reader thread. Only the main thread touches ImGui / message state.

Tools reach the agent over the editor's own MCP server:

- `RuntimeMcpServer` is a Streamable-HTTP MCP server on `127.0.0.1:<mcpPort>/mcp`
  (auto-started when `enableAgent` + `autoStartMcp`). The status bar shows
  `MCP listening on <endpoint>`.
- The backend wires it into the session with `--mcp-config <temp> --strict-mcp-config`,
  where the temp config is `{ "type": "http", "url": ".../mcp", "alwaysLoad": true }`,
  and sets `ENABLE_TOOL_SEARCH=false` in the child environment.
- Why: Claude defers MCP tools behind a tool-search step by default; `alwaysLoad`
  + `ENABLE_TOOL_SEARCH=false` force them to load eagerly so the model can call
  them directly (this also avoids tool-search being silently disabled on models
  or proxies without `tool_reference`).

Hard requirements for tools to actually surface — if any is violated, Claude
drops **all** of the server's tools while `claude mcp list` still reports the
server "✓ Connected":

- Every tool's `inputSchema` must be valid JSON Schema. A property whose value is
  `null` is invalid (watch for the `nlohmann::json` `{}` → `null` trap; use
  `nlohmann::json::object()` for an empty schema) and rejects the whole server.
- Tool names must match `^[A-Za-z0-9_-]+$`. The registry's dotted names
  (`vultra.scene.add_entity`) are sanitized to underscores in `tools/list` and
  mapped back to the dotted form on `tools/call`.

Permissions and guardrails:

- `--permission-mode` is chosen in the chat composer's permission picker (default
  `acceptEdits`: file read/edit allowed, arbitrary shell/network not auto-approved).
  Changing it pushes a best-effort `set_permission_mode` control request to the live
  session and applies on the next start. The real guard for engine/project mutations
  is the capability flags (`allowAgentEngineOperations`, default on /
  `allowAgentProjectOperations`) enforced at the MCP tool-execution layer
  (`handleToolCallOnMainThread`).
- `--append-system-prompt` tells the agent it is the in-editor assistant: drive
  the live scene/engine through the `vultra_*` MCP tools, discover valid kinds via
  the `*_list_*` tools before specifying them, and treat entity references as the
  UUID returned by scene tools (not the numeric runtime instance id).

Diagnostics:

- Set `VULTRA_MCP_LOG` (server) / `VULTRA_BRIDGE_LOG` (bridge) to a file path for
  full request/response logs. (The chat panel no longer surfaces the `system/init`
  MCP-tool-count line; it was a bring-up diagnostic.)

A self-contained `mcp-stdio-bridge` subcommand (stdio↔HTTP) is also available for
stdio-only MCP clients, though the editor uses the HTTP transport directly.
