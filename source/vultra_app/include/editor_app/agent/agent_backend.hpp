#pragma once

#include "editor_app/agent/agent_events.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vultra_app::agent
{
    struct AgentBackendConfig
    {
        std::string           executable {"claude"}; // CLI to launch (resolved via PATH)
        std::string           model;                 // optional model override
        // The agent reaches the editor's tools through an MCP server the backend registers for the
        // user (pre-trusted scope) before the session starts, so the user never configures anything.
        // Both empty -> tool-less chat.
        std::string           mcpServerName; // e.g. "vultra"
        std::string           mcpUrl;        // e.g. "http://127.0.0.1:8848/mcp"
        std::string           allowedToolsGlob;  // optional --allowedTools value
        std::string           permissionMode;    // optional --permission-mode value
        std::string           systemPromptAppend; // optional --append-system-prompt text (agent role)
        std::filesystem::path workingDir;         // optional cwd for the child
    };

    // Abstraction over an "agent brain" the chat panel talks to. v1 ships ClaudeCliBackend;
    // additional vendors (Codex, Gemini, a direct-API backend) become new implementations
    // without touching the panel or the MCP tool layer.
    class IAgentBackend
    {
    public:
        virtual ~IAgentBackend() = default;

        // Spawns/connects the backend. Returns false and fills *error on failure.
        virtual bool start(const AgentBackendConfig& config, std::string* error) = 0;

        // Queues a user turn. Safe to call only from the main thread.
        virtual void sendUserMessage(std::string text) = 0;

        // Non-blocking; returns and clears any events produced since the last call. Call from
        // the main thread each frame.
        virtual std::vector<AgentEvent> drainEvents() = 0;

        // Best-effort cancel of the in-flight turn.
        virtual void interrupt() = 0;

        // Kills the child, joins the reader, clears queues. Idempotent.
        virtual void shutdown() = 0;

        [[nodiscard]] virtual bool isReady() const = 0;
    };
} // namespace vultra_app::agent
