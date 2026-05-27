# Vultra MCP Client Config Examples

MCP servers are started by the AI client, not by the model itself. Register the
`vultra` server in the client configuration, then start a session from the
libvultra repository root.

After the client connects, the first tool call should be:

```text
vultra.bootstrap_context
```

That returns the required Harness entry context, must-read documents, workspace
mode, project paths, and common verification commands.

## Engine Workspace

Use this when working directly in the libvultra repository:

```text
python tools/vultra_mcp/vultra_mcp.py --engine-root .
```

## External Game Project

Use this when the client session starts from a separate Vultra game project:

```text
python C:/path/to/libvultra/tools/vultra_mcp/vultra_mcp.py --engine-root C:/path/to/libvultra --project .
```

The exact JSON wrapper differs by client. Use the examples in this directory as
copyable starting points and adjust absolute paths where needed.
