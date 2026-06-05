#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace vultra_app
{
    // Absolute path to the currently running executable, used to point an MCP client's config
    // at `<this exe> mcp-stdio-bridge`. Returns an empty path if it cannot be determined.
    std::filesystem::path currentExecutablePath();

    // Runs a blocking stdio<->HTTP Model Context Protocol bridge until stdin reaches EOF.
    //
    // The editor's Runtime MCP server speaks a simplified JSON-RPC-over-HTTP dialect (POST /mcp,
    // no SSE / session headers), which a standard MCP client such as Claude Code cannot drive
    // directly. This bridge is launched by that client as a stdio MCP server: it reads
    // newline-delimited JSON-RPC requests from stdin, forwards each as an HTTP POST to the
    // running editor on host:port, and writes the JSON-RPC response back to stdout. It is the
    // same executable invoked as `vultra mcp-stdio-bridge`, so there is no extra dependency.
    //
    // Returns a process exit code (0 on clean EOF).
    int runMcpStdioBridge(const std::string& host, std::uint16_t port);
} // namespace vultra_app
