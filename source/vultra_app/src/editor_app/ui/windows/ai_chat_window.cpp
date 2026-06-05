#include "editor_app/ui/windows/ai_chat_window.hpp"

#include "editor_app/agent/claude_cli_backend.hpp"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <variant>
#include <vector>

namespace vultra_app
{
    namespace
    {
        constexpr ImVec4 kUserColor {0.55f, 0.78f, 1.00f, 1.0f};
        constexpr ImVec4 kAssistantColor {0.86f, 0.90f, 0.95f, 1.0f};
        constexpr ImVec4 kSystemColor {1.00f, 0.78f, 0.35f, 1.0f};
        constexpr ImVec4 kToolColor {0.62f, 0.84f, 0.62f, 1.0f};
        constexpr ImVec4 kErrorColor {1.00f, 0.40f, 0.35f, 1.0f};
        constexpr ImVec4 kHeadingColor {0.62f, 0.82f, 1.00f, 1.0f};
        constexpr ImVec4 kInlineCodeColor {0.95f, 0.82f, 0.55f, 1.0f};

        void wrappedText(const std::string& text, const ImVec4& color)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        // Render one line's inline content with manual word-wrapping. `inline code` gets a boxed
        // background, **bold** is brightened (no bold font is loaded, so emphasis reads via colour).
        void renderInline(const std::string& line, const ImVec4& baseColor)
        {
            struct Run
            {
                std::string text;
                bool        code {false};
                bool        bold {false};
            };
            std::vector<Run> runs;
            {
                std::string cur;
                bool        code = false;
                bool        bold = false;
                for (std::size_t i = 0; i < line.size();)
                {
                    if (line[i] == '`')
                    {
                        if (!cur.empty()) { runs.push_back({cur, code, bold}); cur.clear(); }
                        code = !code;
                        ++i;
                    }
                    else if (!code && i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*')
                    {
                        if (!cur.empty()) { runs.push_back({cur, code, bold}); cur.clear(); }
                        bold = !bold;
                        i += 2;
                    }
                    else
                    {
                        cur.push_back(line[i]);
                        ++i;
                    }
                }
                if (!cur.empty())
                    runs.push_back({cur, code, bold});
            }

            const float wrapX  = ImGui::GetContentRegionAvail().x;
            const float spaceW = ImGui::CalcTextSize(" ").x;
            const float lineH  = ImGui::GetTextLineHeight();
            float       used    = 0.0f;
            bool        atStart = true;

            auto emitWord = [&](const std::string& w, bool code, bool bold) {
                const float ww = ImGui::CalcTextSize(w.c_str()).x;
                if (!atStart)
                {
                    if (used + spaceW + ww > wrapX) { atStart = true; used = 0.0f; }
                    else { ImGui::SameLine(0.0f, spaceW); used += spaceW; }
                }
                const ImVec2 p = ImGui::GetCursorScreenPos();
                if (code)
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(p.x - 2.0f, p.y), ImVec2(p.x + ww + 2.0f, p.y + lineH), IM_COL32(46, 52, 64, 210), 3.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, code ? kInlineCodeColor : (bold ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : baseColor));
                ImGui::TextUnformatted(w.c_str());
                ImGui::PopStyleColor();
                used += ww;
                atStart = false;
            };

            for (const auto& run : runs)
            {
                std::size_t s = 0;
                while (s < run.text.size())
                {
                    const std::size_t e = run.text.find(' ', s);
                    const std::string w = run.text.substr(s, e == std::string::npos ? std::string::npos : e - s);
                    if (!w.empty())
                        emitWord(w, run.code, run.bold);
                    if (e == std::string::npos)
                        break;
                    s = e + 1;
                }
            }
        }

        // Render a prose segment line by line (preserving line breaks), with headings and bullets.
        void renderProse(const std::string& text, const ImVec4& baseColor)
        {
            std::size_t pos = 0;
            while (pos <= text.size())
            {
                const std::size_t nl = text.find('\n', pos);
                std::string       line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                const std::size_t indent  = line.find_first_not_of(' ');
                const std::string trimmed = indent == std::string::npos ? std::string {} : line.substr(indent);

                if (trimmed.empty())
                {
                    ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight() * 0.35f));
                }
                else if (trimmed.rfind("### ", 0) == 0)
                    renderInline(trimmed.substr(4), kHeadingColor);
                else if (trimmed.rfind("## ", 0) == 0)
                    renderInline(trimmed.substr(3), kHeadingColor);
                else if (trimmed.rfind("# ", 0) == 0)
                    renderInline(trimmed.substr(2), kHeadingColor);
                else if (trimmed.rfind("- ", 0) == 0 || trimmed.rfind("* ", 0) == 0)
                {
                    const float ind = static_cast<float>(indent == std::string::npos ? 0 : indent / 2) * 14.0f + 4.0f;
                    ImGui::Indent(ind);
                    renderInline("\xe2\x80\xa2  " + trimmed.substr(2), baseColor); // "• " bullet
                    ImGui::Unindent(ind);
                }
                else
                {
                    renderInline(line, baseColor);
                }

                if (nl == std::string::npos)
                    break;
                pos = nl + 1;
            }
        }

        // imgui_markdown does not handle fenced code blocks; render them as a distinct boxed region.
        void renderCodeBlock(const std::string& code, int idx)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.08f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.84f, 0.76f, 1.0f));
            const std::string id = "##code" + std::to_string(idx);
            if (ImGui::BeginChild(id.c_str(), ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders))
                ImGui::TextUnformatted(code.c_str()); // preserve newlines; no wrap
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }

        // Split the message on ``` fences: prose segments go through imgui_markdown, fenced segments
        // render as boxed code. An unterminated trailing fence (mid-stream) renders as code too.
        void renderRichText(const std::string& text, const ImVec4& color)
        {
            std::size_t pos     = 0;
            int         codeIdx = 0;
            bool        inCode  = false;
            while (pos <= text.size())
            {
                const std::size_t fence = text.find("```", pos);
                std::string       seg   = text.substr(pos, fence == std::string::npos ? std::string::npos : fence - pos);
                if (inCode)
                {
                    // Drop the optional language identifier line (e.g. "json\n") at the top.
                    if (const auto nl = seg.find('\n');
                        nl != std::string::npos && nl <= 16 && seg.substr(0, nl).find(' ') == std::string::npos)
                        seg.erase(0, nl + 1);
                    while (!seg.empty() && (seg.back() == '\n' || seg.back() == '\r'))
                        seg.pop_back();
                    if (!seg.empty())
                        renderCodeBlock(seg, codeIdx++);
                }
                else if (!seg.empty())
                {
                    renderProse(seg, color);
                }
                if (fence == std::string::npos)
                    break;
                pos    = fence + 3;
                inCode = !inCode;
            }
        }

        // A blinking text caret at the current cursor, to signal a live/streaming reply.
        void blinkingCaret()
        {
            if (std::fmod(static_cast<float>(ImGui::GetTime()), 1.0f) < 0.5f)
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float  h = ImGui::GetTextLineHeight();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + 2.0f, p.y + h), ImGui::GetColorU32(ImGuiCol_Text));
            }
            ImGui::Dummy(ImVec2(3.0f, ImGui::GetTextLineHeight()));
        }

        // Animated "Thinking" label with cycling dots.
        std::string animatedThinking()
        {
            const auto dots = static_cast<std::size_t>(ImGui::GetTime() * 2.0) % 4;
            return std::string(ICON_MDI_TIMER_SAND " Thinking") + std::string(dots, '.');
        }

    } // namespace

    AiChatWindow::AiChatWindow() : EditorWindow("AI Chat", ICON_MDI_ROBOT) {}

    bool AiChatWindow::ensureBackend(EditorContext& ctx)
    {
        if (m_Backend && m_Backend->isReady())
            return true;

        m_Backend = std::make_unique<agent::ClaudeCliBackend>();

        agent::AgentBackendConfig config;
        // Trim the configured CLI path: a stray leading/trailing space (easy to leave in the
        // settings text field) would otherwise make the OS fail to find the executable.
        std::string cliPath = ctx.state.editorSettings.agentCliPath;
        cliPath.erase(0, cliPath.find_first_not_of(" \t\r\n"));
        if (const auto last = cliPath.find_last_not_of(" \t\r\n"); last != std::string::npos)
            cliPath.erase(last + 1);
        config.executable = cliPath.empty() ? "claude" : cliPath;
        config.model      = ctx.state.editorSettings.agentModel;
        if (!ctx.state.currentProject.empty())
            config.workingDir = ctx.state.currentProject;

        // Wire the editor's MCP tools in: the backend registers this server for the user (pre-trusted
        // scope) before the session starts, so the agent gets the tools with zero manual setup.
        {
            const auto&       settings   = ctx.state.editorSettings;
            const std::string serverName = settings.mcpServerName.empty() ? "vultra" : settings.mcpServerName;
            const std::string host       = settings.mcpHost.empty() ? "127.0.0.1" : settings.mcpHost;
            const std::string port       = std::to_string(settings.mcpPort <= 0 ? 8848 : settings.mcpPort);
            config.mcpServerName    = serverName;
            config.mcpUrl           = "http://" + host + ":" + port + "/mcp";
            // Tell the agent it is the in-editor assistant and should drive the live editor through
            // the vultra_* MCP tools, not by reading/editing source files.
            config.systemPromptAppend =
                "You are the AI assistant embedded inside the VultraEngine editor. The running editor "
                "exposes its live scene, entities, components, simulation, rendering and assets through "
                "MCP tools named " + serverName + "_* (for example " + serverName + "_runtime_status, " +
                serverName + "_scene_add_entity, " + serverName + "_scene_add_component). For any request "
                "about the live scene or engine, you MUST use these MCP tools to inspect and modify the "
                "running editor directly. Do NOT read or edit source files, and do NOT guess, for runtime "
                "scene/engine operations. Only edit files when the user explicitly asks for code changes.\n"
                "Workflow rules:\n"
                "- DISCOVER before acting: call the list tools first (" + serverName + "_scene_list_entity_kinds, " +
                serverName + "_scene_list_component_kinds) and " + serverName + "_scene_component_metadata to get "
                "the EXACT valid entity_kind / component_kind values and component fields. Never guess kind names "
                "(e.g. do not assume 'MeshRenderer').\n"
                "- Entity references passed to scene tools are the UUID string returned by scene tools, not the "
                "numeric runtime instance id.\n"
                "- Prefer the most specific tool; check " + serverName + "_runtime_status first if unsure of state.";
            config.allowedToolsGlob = "mcp__" + serverName; // server-level grant (all vultra tools)
            // acceptEdits: a dev/creative tool's agent should be able to read & edit project files
            // (and basic mkdir/touch/mv/cp) without a prompt. Our MCP tools are allowed via
            // allowedTools; arbitrary shell/network still isn't auto-approved. It also never blocks
            // on a TTY prompt (headless-safe). The real guard for engine/project mutations is the
            // capability flags enforced at the editor MCP tool layer.
            config.permissionMode = "acceptEdits";
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

    void AiChatWindow::clearConversation()
    {
        if (m_Backend)
        {
            m_Backend->shutdown();
            m_Backend.reset();
        }
        m_Messages.clear();
        m_Status       = Status::NotStarted;
        m_StatusDetail.clear();
        m_AwaitingReply = false;
    }

    void AiChatWindow::submitMessage(EditorContext& ctx, std::string text)
    {
        // Trim trailing whitespace/newlines.
        while (!text.empty() &&
               (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
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

    void AiChatWindow::tick(EditorContext& ctx)
    {
        // Optional automation/testing hook: seed one prompt from the environment on first tick.
        if (!m_AutoPromptChecked)
        {
            m_AutoPromptChecked = true;
            if (ctx.state.editorSettings.enableAgent)
            {
                if (const char* prompt = std::getenv("VULTRA_AI_CHAT_PROMPT"); prompt && *prompt)
                {
                    open() = true;
                    submitMessage(ctx, prompt);
                }
            }
        }

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
        // NOTE: do not force scroll-to-bottom here every frame — that would lock the view to the
        // bottom and prevent scrolling up through history. draw() follows new content only while the
        // user is already at the bottom.
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
                    renderRichText(message.text, kAssistantColor);
                if (message.streaming)
                    blinkingCaret();
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
            submitMessage(ctx, std::string {m_InputBuffer.data()});
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
            // Capture before adding content: was the user already parked at the bottom last frame?
            const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0f;

            ImGui::PushTextWrapPos(0.0f);
            for (std::size_t i = 0; i < m_Messages.size(); ++i)
            {
                if (i > 0)
                {
                    ImGui::Dummy(ImVec2(0.0f, 3.0f));
                    ImGui::Separator();
                    ImGui::Dummy(ImVec2(0.0f, 5.0f));
                }
                drawMessage(m_Messages[i]);
            }
            ImGui::PopTextWrapPos();
            if (m_AwaitingReply)
                ImGui::TextDisabled("%s", animatedThinking().c_str());

            // Follow new content only while the user is at the bottom (or right after they send),
            // so scrolling up to read history is not yanked back down.
            if (m_RequestScrollToBottom || (m_AutoScroll && wasAtBottom))
                ImGui::SetScrollHereY(1.0f);
            m_RequestScrollToBottom = false;
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
    }
} // namespace vultra_app
