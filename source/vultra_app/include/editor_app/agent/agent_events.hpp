#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <variant>

namespace vultra_app::agent
{
    // Incremental assistant text (one or more chunks make up a reply).
    struct TextDelta
    {
        std::string text;
    };

    // Assistant "thinking" text, when the model surfaces reasoning blocks.
    struct Thinking
    {
        std::string text;
    };

    // The model asked to invoke a tool. For MCP tools the name is the namespaced form
    // (e.g. "mcp__vultra__vultra_runtime_status").
    struct ToolUseStarted
    {
        std::string    toolUseId;
        std::string    toolName;
        nlohmann::json input;
    };

    // A tool finished; `summary` is a short human-readable rendering of the result content.
    struct ToolResult
    {
        std::string toolUseId;
        bool        isError {false};
        std::string summary;
    };

    // Terminal event of one assistant turn.
    struct TurnResult
    {
        bool        isError {false};
        std::string sessionId;
        int         numTurns {0};
        double      costUsd {0.0};
    };

    // Backend-level failure: spawn failure, process crash, EOF, or unrecoverable parse error.
    struct BackendError
    {
        std::string message;
        bool        fatal {false};
    };

    using AgentEvent = std::variant<TextDelta, Thinking, ToolUseStarted, ToolResult, TurnResult, BackendError>;
} // namespace vultra_app::agent
