#include "editor_app/agent/claude_cli_backend.hpp"

#include <chrono>
#include <utility>

namespace vultra_app::agent
{
    namespace
    {
        // Flatten an MCP/Anthropic "content" field (string, or array of {type:text,text:...})
        // into a short human-readable summary for a tool-result card.
        std::string summariseContent(const nlohmann::json& content, std::size_t maxLen = 240)
        {
            std::string out;
            if (content.is_string())
            {
                out = content.get<std::string>();
            }
            else if (content.is_array())
            {
                for (const auto& block : content)
                {
                    if (block.is_object() && block.value("type", "") == "text")
                        out += block.value("text", "");
                    else if (block.is_string())
                        out += block.get<std::string>();
                }
            }
            else if (!content.is_null())
            {
                out = content.dump();
            }
            if (out.size() > maxLen)
            {
                out.resize(maxLen);
                out += "...";
            }
            return out;
        }
    } // namespace

    ClaudeCliBackend::~ClaudeCliBackend() { shutdown(); }

    bool ClaudeCliBackend::start(const AgentBackendConfig& config, std::string* error)
    {
        SubprocessOptions options;
        options.executable = config.executable.empty() ? "claude" : config.executable;
        options.args       = {
            "-p",
            "--input-format",
            "stream-json",
            "--output-format",
            "stream-json",
            "--verbose",
        };
        if (!config.model.empty())
        {
            options.args.emplace_back("--model");
            options.args.push_back(config.model);
        }
        if (!config.mcpConfigPath.empty())
        {
            options.args.emplace_back("--mcp-config");
            options.args.push_back(config.mcpConfigPath.string());
        }
        if (!config.allowedToolsGlob.empty())
        {
            options.args.emplace_back("--allowedTools");
            options.args.push_back(config.allowedToolsGlob);
        }
        if (!config.permissionMode.empty())
        {
            options.args.emplace_back("--permission-mode");
            options.args.push_back(config.permissionMode);
        }
        if (!config.workingDir.empty())
            options.workingDir = config.workingDir;

        if (!m_Proc.start(options, error))
            return false;

        m_Ready = true;
        m_Reader = std::jthread([this](std::stop_token stop) { readerLoop(std::move(stop)); });
        return true;
    }

    void ClaudeCliBackend::sendUserMessage(std::string text)
    {
        if (!m_Ready)
        {
            pushEvent(BackendError {.message = "backend is not running", .fatal = false});
            return;
        }
        const nlohmann::json message = {
            {"type", "user"},
            {"message", {{"role", "user"}, {"content", std::move(text)}}},
        };
        if (!m_Proc.writeLine(message.dump()))
            pushEvent(BackendError {.message = "failed to write to agent stdin", .fatal = true});
    }

    std::vector<AgentEvent> ClaudeCliBackend::drainEvents()
    {
        std::deque<AgentEvent> taken;
        {
            std::lock_guard lock {m_Mutex};
            taken.swap(m_Events);
        }
        return {std::make_move_iterator(taken.begin()), std::make_move_iterator(taken.end())};
    }

    void ClaudeCliBackend::interrupt()
    {
        if (!m_Ready)
            return;
        // Control request understood by the Claude Code stream-json input protocol; harmless
        // (ignored) on backends/versions that do not implement it.
        const nlohmann::json control = {
            {"type", "control_request"},
            {"request", {{"subtype", "interrupt"}}},
        };
        m_Proc.writeLine(control.dump());
    }

    void ClaudeCliBackend::shutdown()
    {
        m_Ready = false;
        m_Proc.closeStdin(); // let claude flush and exit on its own first
        if (m_Reader.joinable())
        {
            m_Reader.request_stop();
            m_Reader.join(); // after this only the main thread touches m_Proc
        }
        m_Proc.terminate();
        std::lock_guard lock {m_Mutex};
        m_Events.clear();
    }

    bool ClaudeCliBackend::isReady() const { return m_Ready.load(); }

    void ClaudeCliBackend::pushEvent(AgentEvent ev)
    {
        std::lock_guard lock {m_Mutex};
        m_Events.push_back(std::move(ev));
    }

    void ClaudeCliBackend::readerLoop(std::stop_token stop)
    {
        while (!stop.stop_requested())
        {
            auto line = m_Proc.readLine(std::chrono::milliseconds(200));
            if (line.has_value())
            {
                if (!line->empty())
                    mapStreamJsonLine(*line);
                continue;
            }
            // No line within the window. If the stream ended, the child is done/dead.
            if (m_Proc.eof())
            {
                if (m_Ready.exchange(false))
                {
                    const auto code = m_Proc.exitCode();
                    pushEvent(BackendError {.message = code && *code != 0
                                                           ? "agent process exited with code " + std::to_string(*code)
                                                           : "agent process ended",
                                            .fatal = true});
                }
                break;
            }
        }
    }

    void ClaudeCliBackend::mapStreamJsonLine(const std::string& line)
    {
        nlohmann::json json;
        try
        {
            json = nlohmann::json::parse(line);
        }
        catch (const std::exception&)
        {
            return; // tolerate non-JSON / partial lines rather than aborting the stream
        }
        if (!json.is_object())
            return;

        const std::string type = json.value("type", "");

        if (type == "assistant")
        {
            const auto& content = json.value("message", nlohmann::json::object()).value("content", nlohmann::json::array());
            if (!content.is_array())
                return;
            for (const auto& block : content)
            {
                if (!block.is_object())
                    continue;
                const std::string blockType = block.value("type", "");
                if (blockType == "text")
                {
                    if (auto text = block.value("text", ""); !text.empty())
                        pushEvent(TextDelta {.text = std::move(text)});
                }
                else if (blockType == "thinking")
                {
                    std::string text = block.contains("thinking") ? block.value("thinking", "") : block.value("text", "");
                    if (!text.empty())
                        pushEvent(Thinking {.text = std::move(text)});
                }
                else if (blockType == "tool_use")
                {
                    pushEvent(ToolUseStarted {.toolUseId = block.value("id", ""),
                                              .toolName  = block.value("name", ""),
                                              .input     = block.value("input", nlohmann::json::object())});
                }
            }
        }
        else if (type == "user")
        {
            // Tool results come back wrapped in a synthetic user message.
            const auto& content = json.value("message", nlohmann::json::object()).value("content", nlohmann::json::array());
            if (!content.is_array())
                return;
            for (const auto& block : content)
            {
                if (block.is_object() && block.value("type", "") == "tool_result")
                {
                    pushEvent(ToolResult {.toolUseId = block.value("tool_use_id", ""),
                                          .isError   = block.value("is_error", false),
                                          .summary   = summariseContent(block.value("content", nlohmann::json {}))});
                }
            }
        }
        else if (type == "stream_event")
        {
            // Partial-message deltas (only when --include-partial-messages is set).
            const auto& event = json.value("event", nlohmann::json::object());
            if (event.value("type", "") == "content_block_delta")
            {
                const auto& delta     = event.value("delta", nlohmann::json::object());
                const auto  deltaType = delta.value("type", "");
                if (deltaType == "text_delta")
                {
                    if (auto text = delta.value("text", ""); !text.empty())
                        pushEvent(TextDelta {.text = std::move(text)});
                }
                else if (deltaType == "thinking_delta")
                {
                    if (auto text = delta.value("thinking", ""); !text.empty())
                        pushEvent(Thinking {.text = std::move(text)});
                }
            }
        }
        else if (type == "result")
        {
            pushEvent(TurnResult {.isError   = json.value("subtype", "success") != "success" || json.value("is_error", false),
                                  .sessionId = json.value("session_id", ""),
                                  .numTurns  = json.value("num_turns", 0),
                                  .costUsd   = json.value("total_cost_usd", 0.0)});
        }
        else if (type == "error")
        {
            std::string message = json.contains("error") && json["error"].is_object()
                                      ? json["error"].value("message", "agent error")
                                      : json.value("message", "agent error");
            pushEvent(BackendError {.message = std::move(message), .fatal = false});
        }
        // Unknown types (e.g. "system"/"init") are intentionally ignored.
    }
} // namespace vultra_app::agent
