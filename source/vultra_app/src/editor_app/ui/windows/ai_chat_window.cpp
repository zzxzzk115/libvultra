#include "editor_app/ui/windows/ai_chat_window.hpp"

#include "editor_app/agent/claude_cli_backend.hpp"
#include "editor_app/mcp_stdio_bridge.hpp"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <fstream>
#include <system_error>
#include <variant>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace vultra_app
{
    namespace
    {
        constexpr ImVec4 kUserColor {0.55f, 0.78f, 1.00f, 1.0f};
        constexpr ImVec4 kAssistantColor {0.86f, 0.90f, 0.95f, 1.0f};
        constexpr ImVec4 kSystemColor {1.00f, 0.78f, 0.35f, 1.0f};
        constexpr ImVec4 kToolColor {0.62f, 0.84f, 0.62f, 1.0f};
        constexpr ImVec4 kErrorColor {1.00f, 0.40f, 0.35f, 1.0f};

        void wrappedText(const std::string& text, const ImVec4& color)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        int currentProcessId()
        {
#if defined(_WIN32)
            return ::_getpid();
#else
            return static_cast<int>(::getpid());
#endif
        }

        // Writes a temp MCP config that points Claude at `<this exe> mcp-stdio-bridge`, which
        // forwards JSON-RPC to the editor's HTTP MCP endpoint. Returns the path, or empty on
        // failure (caller then falls back to a tool-less chat).
        std::filesystem::path writeMcpConfig(EditorContext& ctx)
        {
            const auto exe = currentExecutablePath();
            if (exe.empty())
                return {};

            const auto&       settings   = ctx.state.editorSettings;
            const std::string serverName = settings.mcpServerName.empty() ? "vultra" : settings.mcpServerName;
            const std::string host       = settings.mcpHost.empty() ? "127.0.0.1" : settings.mcpHost;
            const std::string port       = std::to_string(settings.mcpPort <= 0 ? 8848 : settings.mcpPort);

            const nlohmann::json config = {
                {"mcpServers",
                 {{serverName,
                   {{"type", "stdio"},
                    {"command", exe.string()},
                    {"args", nlohmann::json::array({"mcp-stdio-bridge", "--host", host, "--port", port})}}}}}};

            std::error_code ec;
            auto            path = std::filesystem::temp_directory_path(ec);
            if (ec)
                return {};
            path /= "vultra_mcp_" + serverName + "_" + std::to_string(currentProcessId()) + ".json";

            std::ofstream out {path, std::ios::binary | std::ios::trunc};
            if (!out)
                return {};
            out << config.dump(2);
            if (!out)
                return {};
            return path;
        }
    } // namespace

    AiChatWindow::AiChatWindow() : EditorWindow("AI Chat", ICON_MDI_ROBOT) {}

    bool AiChatWindow::ensureBackend(EditorContext& ctx)
    {
        if (m_Backend && m_Backend->isReady())
            return true;

        m_Backend = std::make_unique<agent::ClaudeCliBackend>();

        agent::AgentBackendConfig config;
        config.executable =
            ctx.state.editorSettings.agentCliPath.empty() ? "claude" : ctx.state.editorSettings.agentCliPath;
        config.model = ctx.state.editorSettings.agentModel;
        if (!ctx.state.currentProject.empty())
            config.workingDir = ctx.state.currentProject;

        // Wire the editor's MCP tools in via the stdio bridge. If config generation fails we
        // still launch a tool-less chat rather than blocking the panel entirely.
        m_McpConfigPath = writeMcpConfig(ctx);
        if (!m_McpConfigPath.empty())
        {
            const std::string serverName =
                ctx.state.editorSettings.mcpServerName.empty() ? "vultra" : ctx.state.editorSettings.mcpServerName;
            config.mcpConfigPath = m_McpConfigPath;
            config.allowedToolsGlob = "mcp__" + serverName; // server-level grant (all vultra tools)
            // dontAsk is the only headless mode that never blocks on a TTY prompt: it auto-allows
            // everything in allowedTools and denies the rest. The real guard for mutating tools is
            // the capability flags enforced at the editor tool layer.
            config.permissionMode = "dontAsk";
        }

        std::string error;
        if (!m_Backend->start(config, &error))
        {
            m_Backend.reset();
            m_Status       = Status::Error;
            m_StatusDetail = error.empty() ? "Failed to launch 'claude'. Is the Claude CLI installed and on PATH?"
                                           : error + " — is the Claude CLI installed and on PATH?";
            return false;
        }

        m_Status = Status::Ready;
        m_StatusDetail.clear();
        return true;
    }

    namespace
    {
        void removeTempConfig(std::filesystem::path& path)
        {
            if (path.empty())
                return;
            std::error_code ec;
            std::filesystem::remove(path, ec);
            path.clear();
        }
    } // namespace

    void AiChatWindow::clearConversation()
    {
        if (m_Backend)
        {
            m_Backend->shutdown();
            m_Backend.reset();
        }
        removeTempConfig(m_McpConfigPath);
        m_Messages.clear();
        m_Status       = Status::NotStarted;
        m_StatusDetail.clear();
        m_AwaitingReply = false;
    }

    AiChatWindow::ChatMessage& AiChatWindow::currentAssistantMessage()
    {
        if (!m_Messages.empty() && m_Messages.back().role == ChatMessage::Role::Assistant && m_Messages.back().streaming)
            return m_Messages.back();
        ChatMessage message;
        message.role      = ChatMessage::Role::Assistant;
        message.streaming = true;
        m_Messages.push_back(std::move(message));
        return m_Messages.back();
    }

    void AiChatWindow::tick(EditorContext& /*ctx*/)
    {
        if (!m_Backend)
            return;

        for (auto& event : m_Backend->drainEvents())
        {
            std::visit(
                [&](auto&& ev) {
                    using T = std::decay_t<decltype(ev)>;
                    if constexpr (std::is_same_v<T, agent::TextDelta>)
                    {
                        currentAssistantMessage().text += ev.text;
                        m_AwaitingReply = false;
                    }
                    else if constexpr (std::is_same_v<T, agent::Thinking>)
                    {
                        currentAssistantMessage().thinking += ev.text;
                    }
                    else if constexpr (std::is_same_v<T, agent::ToolUseStarted>)
                    {
                        auto& message = currentAssistantMessage();
                        message.tools.push_back(ToolInvocation {.id    = ev.toolUseId,
                                                                .name  = ev.toolName,
                                                                .input = ev.input,
                                                                .state = ToolInvocation::State::Running});
                        m_AwaitingReply = false;
                    }
                    else if constexpr (std::is_same_v<T, agent::ToolResult>)
                    {
                        for (auto it = m_Messages.rbegin(); it != m_Messages.rend(); ++it)
                        {
                            bool found = false;
                            for (auto& tool : it->tools)
                            {
                                if (tool.id == ev.toolUseId)
                                {
                                    tool.result = ev.summary;
                                    tool.state  = ev.isError ? ToolInvocation::State::Error : ToolInvocation::State::Done;
                                    found       = true;
                                    break;
                                }
                            }
                            if (found)
                                break;
                        }
                    }
                    else if constexpr (std::is_same_v<T, agent::TurnResult>)
                    {
                        if (!m_Messages.empty() && m_Messages.back().role == ChatMessage::Role::Assistant)
                            m_Messages.back().streaming = false;
                        m_AwaitingReply = false;
                    }
                    else if constexpr (std::is_same_v<T, agent::BackendError>)
                    {
                        ChatMessage message;
                        message.role = ChatMessage::Role::System;
                        message.text = ev.message;
                        m_Messages.push_back(std::move(message));
                        m_AwaitingReply = false;
                        if (ev.fatal)
                        {
                            m_Status       = Status::Error;
                            m_StatusDetail = ev.message;
                            if (!m_Messages.empty() && m_Messages.front().role == ChatMessage::Role::Assistant)
                                m_Messages.front().streaming = false;
                        }
                    }
                },
                event);
        }

        if (m_AutoScroll)
            m_RequestScrollToBottom = true;
    }

    void AiChatWindow::drawStatusBanner(EditorContext& ctx)
    {
        if (!ctx.state.editorSettings.enableAgent)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kSystemColor);
            ImGui::TextWrapped("%s Agent features are disabled. Enable the agent in Editor Settings to chat.",
                               ICON_MDI_INFORMATION_OUTLINE);
            ImGui::PopStyleColor();
            ImGui::Separator();
            return;
        }

        if (m_Status == Status::Error && !m_StatusDetail.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
            ImGui::TextWrapped("%s %s", ICON_MDI_ALERT, m_StatusDetail.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_RELOAD " Restart"))
            {
                m_Status = Status::NotStarted;
                m_StatusDetail.clear();
                if (m_Backend)
                {
                    m_Backend->shutdown();
                    m_Backend.reset();
                }
            }
            ImGui::Separator();
        }
    }

    void AiChatWindow::drawToolCard(const ToolInvocation& tool)
    {
        const char* icon = ICON_MDI_WRENCH;
        ImVec4      color = kToolColor;
        switch (tool.state)
        {
            case ToolInvocation::State::Running:
                icon = ICON_MDI_TIMER_SAND;
                break;
            case ToolInvocation::State::Done:
                icon = ICON_MDI_CHECK_CIRCLE_OUTLINE;
                break;
            case ToolInvocation::State::Error:
                icon  = ICON_MDI_CLOSE_OCTAGON;
                color = kErrorColor;
                break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        const bool open = ImGui::TreeNodeEx(&tool,
                                            ImGuiTreeNodeFlags_SpanAvailWidth,
                                            "%s %s",
                                            icon,
                                            tool.name.empty() ? "tool" : tool.name.c_str());
        ImGui::PopStyleColor();
        if (open)
        {
            if (!tool.input.is_null())
                wrappedText("input: " + tool.input.dump(2), ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
            if (!tool.result.empty())
                wrappedText("result: " + tool.result, ImVec4(0.70f, 0.80f, 0.74f, 1.0f));
            ImGui::TreePop();
        }
    }

    void AiChatWindow::drawMessage(const ChatMessage& message)
    {
        switch (message.role)
        {
            case ChatMessage::Role::User:
                ImGui::PushStyleColor(ImGuiCol_Text, kUserColor);
                ImGui::TextUnformatted(ICON_MDI_ACCOUNT " You");
                ImGui::PopStyleColor();
                wrappedText(message.text, kUserColor);
                break;
            case ChatMessage::Role::Assistant:
                ImGui::PushStyleColor(ImGuiCol_Text, kAssistantColor);
                ImGui::TextUnformatted(ICON_MDI_ROBOT " Claude");
                ImGui::PopStyleColor();
                if (!message.thinking.empty())
                    wrappedText(message.thinking, ImVec4(0.55f, 0.58f, 0.64f, 1.0f));
                for (const auto& tool : message.tools)
                    drawToolCard(tool);
                if (!message.text.empty())
                    wrappedText(message.text, kAssistantColor);
                if (message.streaming)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(ICON_MDI_DOTS_HORIZONTAL);
                }
                break;
            case ChatMessage::Role::System:
                wrappedText(std::string(ICON_MDI_INFORMATION_OUTLINE " ") + message.text, kSystemColor);
                break;
        }
        ImGui::Spacing();
    }

    void AiChatWindow::drawComposer(EditorContext& ctx)
    {
        const bool agentEnabled = ctx.state.editorSettings.enableAgent;

        auto submit = [&]() {
            std::string text = m_InputBuffer.data();
            // Trim trailing whitespace/newlines.
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
                text.pop_back();
            if (text.empty())
                return;
            if (!ensureBackend(ctx))
                return;

            ChatMessage userMessage;
            userMessage.role = ChatMessage::Role::User;
            userMessage.text = text;
            m_Messages.push_back(std::move(userMessage));
            m_Backend->sendUserMessage(std::move(text));
            m_AwaitingReply         = true;
            m_RequestScrollToBottom = true;
            m_InputBuffer.fill('\0');
        };

        if (!agentEnabled)
            ImGui::BeginDisabled();

        const float buttonsWidth = 132.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonsWidth);
        const bool entered = ImGui::InputTextMultiline("##AiChatInput",
                                                       m_InputBuffer.data(),
                                                       m_InputBuffer.size(),
                                                       ImVec2(0.0f, ImGui::GetTextLineHeight() * 3.0f),
                                                       ImGuiInputTextFlags_CtrlEnterForNewLine |
                                                           ImGuiInputTextFlags_EnterReturnsTrue);
        if (entered)
            submit();

        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Button(ICON_MDI_SEND " Send", ImVec2(buttonsWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f)))
            submit();
        if (m_AwaitingReply && m_Backend)
        {
            if (ImGui::Button(ICON_MDI_STOP " Stop", ImVec2(buttonsWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f)))
                m_Backend->interrupt();
        }
        else if (ImGui::Button(ICON_MDI_DELETE_SWEEP " Clear", ImVec2(buttonsWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f)))
        {
            clearConversation();
        }
        ImGui::EndGroup();

        if (!agentEnabled)
            ImGui::EndDisabled();

        ImGui::TextDisabled("Enter to send, Ctrl+Enter for newline.");
    }

    void AiChatWindow::draw(EditorContext& ctx)
    {
        if (!ImGui::Begin(title().c_str(), &m_Open))
        {
            ImGui::End();
            return;
        }

        drawStatusBanner(ctx);

        const float composerHeight = ImGui::GetTextLineHeightWithSpacing() * 4.5f;
        if (ImGui::BeginChild("##AiChatScroll", ImVec2(0.0f, -composerHeight), ImGuiChildFlags_Borders))
        {
            for (const auto& message : m_Messages)
                drawMessage(message);
            if (m_AwaitingReply)
                ImGui::TextDisabled(ICON_MDI_TIMER_SAND " Thinking...");
            if (m_RequestScrollToBottom && ImGui::GetScrollMaxY() > 0.0f)
            {
                ImGui::SetScrollHereY(1.0f);
                m_RequestScrollToBottom = false;
            }
        }
        ImGui::EndChild();

        drawComposer(ctx);

        ImGui::End();
    }

    void AiChatWindow::onClosed(EditorContext& /*ctx*/)
    {
        // Closing the panel frees the child process; reopening + sending respawns it.
        if (m_Backend)
        {
            m_Backend->shutdown();
            m_Backend.reset();
        }
        removeTempConfig(m_McpConfigPath);
        m_Status = Status::NotStarted;
        m_AwaitingReply = false;
    }

    void AiChatWindow::onDestroy(EditorContext& /*ctx*/)
    {
        if (m_Backend)
        {
            m_Backend->shutdown();
            m_Backend.reset();
        }
        removeTempConfig(m_McpConfigPath);
    }
} // namespace vultra_app
