#include "editor_app/ui/windows/ai_chat_window.hpp"

#include "editor_app/agent/claude_cli_backend.hpp"

#include <vultra/core/i18n/i18n.hpp>
#include <vultra/function/imgui/imgui_dpi.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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

        // Friendly name of the agent backing the chat, derived from its CLI. "claude" -> "Claude";
        // any other agent CLI -> its capitalized stem, so the panel introduces itself correctly.
        std::string agentDisplayName(std::string_view cliPath)
        {
            std::string stem = std::filesystem::path(cliPath).stem().generic_string();
            if (stem.empty())
                stem = "claude";
            std::string lower = stem;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (lower == "claude")
                return "Claude";
            stem[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(stem[0])));
            return stem;
        }

        void wrappedText(const std::string& text, const ImVec4& color)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        // Decode one UTF-8 code point at byte `i`; returns its byte length (>=1).
        std::size_t decodeUtf8(const std::string& s, std::size_t i, std::uint32_t& cp)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) { cp = c; return 1; }
            if ((c >> 5) == 0x6 && i + 1 < s.size())
            {
                cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
                return 2;
            }
            if ((c >> 4) == 0xE && i + 2 < s.size())
            {
                cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                     (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
                return 3;
            }
            if ((c >> 3) == 0x1E && i + 3 < s.size())
            {
                cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                     ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
                     (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
                return 4;
            }
            cp = c; // malformed lead byte - consume one byte so we always make progress
            return 1;
        }

        // Wide / CJK code points wrap between any two characters (no spaces to break on). Covers
        // Han, Kana, Hangul, and the CJK symbol/fullwidth blocks.
        bool isCjkWide(std::uint32_t cp)
        {
            return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
                   (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
                   (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
                   (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                   (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
                   (cp >= 0xFFE0 && cp <= 0xFFE6);
        }

        // Render one line's inline content with manual wrapping. `inline code` gets a boxed
        // background, **bold** is brightened (no bold font is loaded, so emphasis reads via colour).
        // Wrapping breaks on spaces between Latin words AND between CJK characters, so Chinese /
        // Japanese / Korean prose (which has no spaces) folds instead of running off the edge.
        void renderInline(const std::string& line, const ImVec4& baseColor)
        {
            // A piece is the smallest unit that may begin a new visual line: a run of Latin word
            // characters, or a single CJK glyph. `gap` marks a space that precedes it on the line.
            struct Piece
            {
                std::string text;
                bool        code {false};
                bool        bold {false};
                bool        gap {false};
            };
            std::vector<Piece> pieces;

            bool code = false;
            bool bold = false;
            bool pendingGap = false;
            for (std::size_t i = 0; i < line.size();)
            {
                const char ch = line[i];
                if (ch == '`')
                {
                    code = !code;
                    ++i;
                    continue;
                }
                if (!code && i + 1 < line.size() && ch == '*' && line[i + 1] == '*')
                {
                    bold = !bold;
                    i += 2;
                    continue;
                }
                if (ch == ' ' || ch == '\t')
                {
                    pendingGap = true;
                    ++i;
                    continue;
                }

                std::uint32_t     cp  = 0;
                const std::size_t len = decodeUtf8(line, i, cp);
                if (isCjkWide(cp))
                {
                    pieces.push_back({line.substr(i, len), code, bold, pendingGap});
                    pendingGap = false;
                    i += len;
                    continue;
                }
                // Accumulate a Latin/ASCII word up to the next space, CJK glyph, or style marker.
                const std::size_t start = i;
                while (i < line.size())
                {
                    const char wc = line[i];
                    if (wc == ' ' || wc == '\t' || wc == '`')
                        break;
                    if (!code && i + 1 < line.size() && wc == '*' && line[i + 1] == '*')
                        break;
                    std::uint32_t     wcp  = 0;
                    const std::size_t wlen = decodeUtf8(line, i, wcp);
                    if (isCjkWide(wcp))
                        break;
                    i += wlen;
                }
                pieces.push_back({line.substr(start, i - start), code, bold, pendingGap});
                pendingGap = false;
            }

            const float wrapX  = ImGui::GetContentRegionAvail().x;
            const float spaceW = ImGui::CalcTextSize(" ").x;
            const float lineH  = ImGui::GetTextLineHeight();
            float       used    = 0.0f;
            bool        atStart = true;

            for (const auto& piece : pieces)
            {
                const float ww  = ImGui::CalcTextSize(piece.text.c_str()).x;
                float       gap = (piece.gap && !atStart) ? spaceW : 0.0f;
                if (!atStart && used + gap + ww > wrapX)
                {
                    atStart = true;
                    used    = 0.0f;
                    gap     = 0.0f;
                }
                if (!atStart)
                {
                    ImGui::SameLine(0.0f, gap);
                    used += gap;
                }
                const ImVec2 p = ImGui::GetCursorScreenPos();
                if (piece.code)
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(p.x - vultra::ui::dp(2.0f), p.y),
                    ImVec2(p.x + ww + vultra::ui::dp(2.0f), p.y + lineH),
                    IM_COL32(46, 52, 64, 210),
                    vultra::ui::dp(3.0f));
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      piece.code ? kInlineCodeColor
                                                 : (piece.bold ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : baseColor));
                ImGui::TextUnformatted(piece.text.c_str());
                ImGui::PopStyleColor();
                used += ww;
                atStart = false;
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
                    const float ind = static_cast<float>(indent == std::string::npos ? 0 : indent / 2) * vultra::ui::dp(14.0f) + vultra::ui::dp(4.0f);
                    ImGui::Indent(ind);
                    renderInline("\xe2\x80\xa2  " + trimmed.substr(2), baseColor); // leading bullet glyph
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
                    ImVec2(p.x, p.y + vultra::ui::dp(1.0f)),
                    ImVec2(p.x + vultra::ui::dp(2.0f), p.y + h),
                    ImGui::GetColorU32(ImGuiCol_Text));
            }
            ImGui::Dummy(ImVec2(vultra::ui::dp(3.0f), ImGui::GetTextLineHeight()));
        }

        // Animated "Thinking" label with cycling dots.
        std::string animatedThinking()
        {
            const auto dots = static_cast<std::size_t>(ImGui::GetTime() * 2.0) % 4;
            return std::string(vultra::tr("aiChat.thinking")) + std::string(dots, '.');
        }

        // A small rotating arc spinner drawn at the cursor; advances Y like a one-line widget.
        void spinner(float radius, float thickness, ImU32 color)
        {
            const ImVec2 pos    = ImGui::GetCursorScreenPos();
            const float  pad    = ImGui::GetStyle().FramePadding.y;
            const ImVec2 center = ImVec2(pos.x + radius, pos.y + radius + pad);
            const auto   t      = static_cast<float>(ImGui::GetTime());
            const float  start  = std::fmod(t * 5.5f, 6.2831853f);
            constexpr int kSegs = 24;
            constexpr int kArc  = 16; // ~2/3 of a circle so the gap reads as motion
            ImDrawList*  dl     = ImGui::GetWindowDrawList();
            dl->PathClear();
            for (int i = 0; i <= kArc; ++i)
            {
                const float a = start + (static_cast<float>(i) / kSegs) * 6.2831853f;
                dl->PathLineTo(ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius));
            }
            dl->PathStroke(color, 0, thickness);
            ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f + pad));
        }

        // Spinner + "Thinking..." on one baseline; the live "AI is working" indicator.
        void thinkingIndicator()
        {
            spinner(vultra::ui::dp(7.0f), vultra::ui::dp(2.5f), ImGui::GetColorU32(kSystemColor));
            ImGui::SameLine(0.0f, vultra::ui::dp(8.0f));
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, kSystemColor);
            ImGui::TextUnformatted(animatedThinking().c_str());
            ImGui::PopStyleColor();
        }

        // The Claude permission modes offered by the composer picker, in escalating-trust order.
        struct PermissionOption
        {
            const char* mode;     // value passed to the CLI (--permission-mode)
            const char* labelKey; // i18n key for the short menu label
            const char* icon;     // leading glyph
            const char* hintKey;  // i18n key for the tooltip
        };
        constexpr PermissionOption kPermissionOptions[] = {
            {"default", "aiChat.permission.ask.label", ICON_MDI_SHIELD_CHECK, "aiChat.permission.ask.hint"},
            {"acceptEdits", "aiChat.permission.acceptEdits.label", ICON_MDI_PENCIL_OUTLINE,
             "aiChat.permission.acceptEdits.hint"},
            {"plan", "aiChat.permission.plan.label", ICON_MDI_MAP_OUTLINE, "aiChat.permission.plan.hint"},
            {"bypassPermissions", "aiChat.permission.bypass.label", ICON_MDI_FLASH_OUTLINE,
             "aiChat.permission.bypass.hint"},
        };

        const PermissionOption& permissionOptionFor(const std::string& mode)
        {
            for (const auto& option : kPermissionOptions)
                if (mode == option.mode)
                    return option;
            return kPermissionOptions[1]; // acceptEdits fallback
        }

    } // namespace

    AiChatWindow::AiChatWindow() : EditorWindow("AI Chat", ICON_MDI_ROBOT, "window.aiChat") {}

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
            // capability flags enforced at the editor MCP tool layer. The concrete mode is the
            // one chosen in the composer's permission picker (defaults to acceptEdits).
            config.permissionMode = m_PermissionMode;
        }

        std::string error;
        if (!m_Backend->start(config, &error))
        {
            m_Backend.reset();
            m_Status       = Status::Error;
            m_StatusDetail = error.empty() ? vultra::tr("aiChat.status.launchFailed")
                                           : error + vultra::tr("aiChat.status.launchFailedSuffix");
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
        m_TurnActive = false;
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
        m_TurnActive            = true;
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

        // Keep typing out any in-flight reply even with no backend (e.g. panel closed mid-reveal),
        // so reopening shows the message fully instead of stuck at a partial prefix.
        advanceReveal();

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
                        m_TurnActive = false;
                    }
                    else if constexpr (std::is_same_v<T, agent::BackendError>)
                    {
                        ChatMessage message;
                        message.role = ChatMessage::Role::System;
                        message.text = ev.message;
                        m_Messages.push_back(std::move(message));
                        // A non-fatal notice (e.g. a transient tool warning) does NOT end the turn -
                        // leave m_TurnActive so the Stop button and Thinking indicator persist.
                        if (ev.fatal)
                        {
                            m_TurnActive   = false;
                            m_Status       = Status::Error;
                            m_StatusDetail = ev.message;
                            if (!m_Messages.empty() && m_Messages.front().role == ChatMessage::Role::Assistant)
                                m_Messages.front().streaming = false;
                        }
                    }
                },
                event);
        }
        // NOTE: do not force scroll-to-bottom here every frame - that would lock the view to the
        // bottom and prevent scrolling up through history. draw() follows new content only while the
        // user is already at the bottom.
    }

    void AiChatWindow::advanceReveal()
    {
        // Type the assistant text out: walk each message's `revealed` byte count toward its full
        // length. The rate scales with how far behind we are so a long reply never lags for seconds,
        // yet a steadily streamed one still reads like a typewriter. Runs every tick (window-visible
        // or not) so a reply that finished while hidden is already fully revealed on return.
        const float dt = ImGui::GetIO().DeltaTime;
        if (dt <= 0.0f)
            return;
        for (auto& message : m_Messages)
        {
            if (message.role != ChatMessage::Role::Assistant)
                continue;
            const double target = static_cast<double>(message.text.size());
            if (message.revealed >= target)
            {
                message.revealed = target;
                continue;
            }
            const double remaining = target - message.revealed;
            // ~90 bytes/s floor, but always finish the current backlog within ~0.35s.
            const double cps = std::max(90.0, remaining / 0.35);
            message.revealed = std::min(target, message.revealed + cps * static_cast<double>(dt));
        }
    }

    void AiChatWindow::drawStatusBanner(EditorContext& ctx)
    {
        if (!ctx.state.editorSettings.enableAgent)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kSystemColor);
            ImGui::TextWrapped("%s %s", ICON_MDI_INFORMATION_OUTLINE, vultra::tr("aiChat.banner.agentDisabled"));
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
            if (ImGui::SmallButton((std::string {ICON_MDI_RELOAD " "} + vultra::tr("aiChat.restart")).c_str()))
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

    void AiChatWindow::drawWelcome(EditorContext& ctx)
    {
        static_cast<void>(ctx);
        // Friendly empty-state: who the assistant is, what it can do, and a clickable example.
        ImGui::Dummy(ImVec2(0.0f, vultra::ui::dp(6.0f)));

        ImGui::PushStyleColor(ImGuiCol_Text, kHeadingColor);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s %s",
                           ICON_MDI_HAND_WAVE,
                           vultra::trf("aiChat.welcome.greeting", m_AgentName).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, vultra::ui::dp(4.0f)));
        wrappedText(vultra::tr("aiChat.welcome.capabilities"), kAssistantColor);

        ImGui::Dummy(ImVec2(0.0f, vultra::ui::dp(8.0f)));
        ImGui::PushStyleColor(ImGuiCol_Text, kSystemColor);
        ImGui::TextWrapped("%s %s", ICON_MDI_LIGHTBULB_ON_OUTLINE, vultra::tr("aiChat.welcome.exampleLabel"));
        ImGui::PopStyleColor();

        // The example prompt, quoted; a button drops it straight into the composer.
        const char* example = vultra::tr("aiChat.welcome.example");
        wrappedText(std::string {"\""} + example + "\"", kUserColor);
        if (ImGui::SmallButton(
                (std::string {ICON_MDI_ARROW_UP_BOLD " "} + vultra::tr("aiChat.welcome.useExample")).c_str()))
        {
            std::snprintf(m_InputBuffer.data(), m_InputBuffer.size(), "%s", example);
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
                                            tool.name.empty() ? vultra::tr("aiChat.tool.fallbackName") : tool.name.c_str());
        ImGui::PopStyleColor();
        if (open)
        {
            if (!tool.input.is_null())
                wrappedText(vultra::tr("aiChat.tool.inputPrefix") + tool.input.dump(2), ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
            if (!tool.result.empty())
                wrappedText(vultra::tr("aiChat.tool.resultPrefix") + tool.result, ImVec4(0.70f, 0.80f, 0.74f, 1.0f));
            ImGui::TreePop();
        }
    }

    void AiChatWindow::drawMessage(const ChatMessage& message, std::size_t index)
    {
        // System notices are slim inline lines, not bubbles - they are diagnostics, not turns.
        if (message.role == ChatMessage::Role::System)
        {
            wrappedText(std::string(ICON_MDI_INFORMATION_OUTLINE " ") + message.text, kSystemColor);
            ImGui::Spacing();
            return;
        }

        const bool   isUser = message.role == ChatMessage::Role::User;
        const ImVec4 accent = isUser ? kUserColor : kAssistantColor;

        // Avatar + name header, so every turn is unmistakably one speaker.
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::TextUnformatted(isUser ? (std::string {ICON_MDI_ACCOUNT " "} + vultra::tr("aiChat.you")).c_str()
                                      : (std::string {ICON_MDI_ROBOT " "} + m_AgentName).c_str());
        ImGui::PopStyleColor();

        // The turn's body sits in a rounded, tinted bubble so replies read as distinct cards
        // instead of a wall of text. Auto-resizes to its content height.
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              isUser ? ImVec4(0.13f, 0.17f, 0.24f, 0.55f) : ImVec4(0.11f, 0.13f, 0.17f, 0.70f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, vultra::ui::dp(7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(vultra::ui::dp(9.0f), vultra::ui::dp(7.0f)));
        const std::string childId = "##bubble" + std::to_string(index);
        if (ImGui::BeginChild(childId.c_str(), ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders))
        {
            if (isUser)
            {
                renderProse(message.text, kUserColor);
            }
            else
            {
                if (!message.thinking.empty())
                    wrappedText(message.thinking, ImVec4(0.55f, 0.58f, 0.64f, 1.0f));
                for (const auto& tool : message.tools)
                    drawToolCard(tool);

                // Typewriter: render only the revealed prefix while it streams/catches up. The
                // markdown renderer already tolerates a mid-token cut, so a brief partial glyph is fine.
                const std::size_t shown =
                    std::min(message.text.size(), static_cast<std::size_t>(message.revealed));
                const bool stillTyping = message.streaming || shown < message.text.size();
                if (shown > 0)
                    renderRichText(message.text.substr(0, shown), kAssistantColor);
                // While a fresh reply is still empty (only the header so far), show the spinner
                // inside the bubble; the caret takes over once text starts flowing.
                if (shown == 0 && message.streaming && message.tools.empty() && message.thinking.empty())
                    thinkingIndicator();
                else if (stillTyping)
                    blinkingCaret();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    void AiChatWindow::drawPermissionPicker(EditorContext& ctx, const ImVec2& size)
    {
        const PermissionOption& current = permissionOptionFor(m_PermissionMode);
        const std::string       label   = std::string(current.icon) + " " + vultra::tr(current.labelKey) + " " ICON_MDI_MENU_DOWN
                                    "##aiPermPicker";
        if (ImGui::Button(label.c_str(), size))
            ImGui::OpenPopup("##aiPermPopup");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", vultra::tr("aiChat.permission.tooltip"));

        if (ImGui::BeginPopup("##aiPermPopup"))
        {
            ImGui::TextDisabled("%s", vultra::tr("aiChat.permission.header"));
            ImGui::Separator();
            for (const auto& option : kPermissionOptions)
            {
                const bool        selected = m_PermissionMode == option.mode;
                const std::string item     = std::string(option.icon) + "   " + vultra::tr(option.labelKey);
                if (ImGui::MenuItem(item.c_str(), nullptr, selected) && !selected)
                {
                    m_PermissionMode = option.mode;
                    // Push it onto the live session if one is running; otherwise it is applied when
                    // the backend next starts (ensureBackend reads m_PermissionMode).
                    if (m_Backend)
                        m_Backend->setPermissionMode(m_PermissionMode);
                    ctx.state.statusMessage =
                        vultra::trf("aiChat.permission.statusMessage", vultra::tr(option.labelKey));
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", vultra::tr(option.hintKey));
            }
            ImGui::EndPopup();
        }
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

        // Full-width prompt box; Enter sends, Ctrl+Enter inserts a newline.
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        const bool entered = ImGui::InputTextMultiline("##AiChatInput",
                                                       m_InputBuffer.data(),
                                                       m_InputBuffer.size(),
                                                       ImVec2(0.0f, ImGui::GetTextLineHeight() * 3.0f),
                                                       ImGuiInputTextFlags_CtrlEnterForNewLine |
                                                           ImGuiInputTextFlags_EnterReturnsTrue);
        if (entered)
            submit();

        // Control row below the box: [new chat] ........ [permission picker][send/stop].
        const ImGuiStyle& style = ImGui::GetStyle();
        const float       rowH  = ImGui::GetFrameHeight();
        const float       sendW = rowH;    // square icon button
        const float       permW = vultra::ui::dp(170.0f);  // fits "Accept Edits  v"
        const bool        busy  = m_TurnActive && m_Backend;

        if (ImGui::Button(ICON_MDI_BROOM "##aiNewChat", ImVec2(rowH, rowH)))
            clearConversation();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", vultra::tr("aiChat.newChat.tooltip"));

        // Right-align the permission picker + send/stop cluster.
        const float clusterW = permW + style.ItemSpacing.x + sendW;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - clusterW);

        drawPermissionPicker(ctx, ImVec2(permW, rowH));
        ImGui::SameLine();

        // One button that flips between Send and a red Stop while the agent works.
        if (busy)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.22f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.30f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.68f, 0.16f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button(ICON_MDI_STOP "##aiStop", ImVec2(sendW, rowH)))
                m_Backend->interrupt();
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", vultra::tr("aiChat.stop.tooltip"));
        }
        else
        {
            if (ImGui::Button(ICON_MDI_SEND "##aiSend", ImVec2(sendW, rowH)))
                submit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", vultra::tr("aiChat.send.tooltip"));
        }

        if (!agentEnabled)
            ImGui::EndDisabled();

        ImGui::TextDisabled("%s", vultra::tr("aiChat.composer.hint"));
    }

    void AiChatWindow::draw(EditorContext& ctx)
    {
        if (!ImGui::Begin(title().c_str(), &m_Open))
        {
            ImGui::End();
            return;
        }

        // Keep the displayed agent identity in sync with the configured CLI (e.g. "Claude").
        m_AgentName = agentDisplayName(ctx.state.editorSettings.agentCliPath);

        drawStatusBanner(ctx);

        // Reserve exactly the composer's stack: 3-line input + control-button row + hint line.
        const ImGuiStyle& style          = ImGui::GetStyle();
        const float       composerHeight = ImGui::GetTextLineHeight() * 3.0f + ImGui::GetFrameHeight() +
                                     ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y * 3.0f;
        if (ImGui::BeginChild("##AiChatScroll", ImVec2(0.0f, -composerHeight), ImGuiChildFlags_Borders))
        {
            // Capture before adding content: was the user already parked at the bottom last frame?
            const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - vultra::ui::dp(8.0f);

            ImGui::PushTextWrapPos(0.0f);
            if (m_Messages.empty())
                drawWelcome(ctx);
            for (std::size_t i = 0; i < m_Messages.size(); ++i)
            {
                if (i > 0)
                    ImGui::Dummy(ImVec2(0.0f, vultra::ui::dp(4.0f)));
                drawMessage(m_Messages[i], i);
            }
            ImGui::PopTextWrapPos();
            // Spinner sits after the user's bubble until the assistant bubble appears; once it does,
            // that bubble carries its own caret/spinner, so this avoids a duplicate indicator.
            const bool assistantBubbleLive =
                !m_Messages.empty() && m_Messages.back().role == ChatMessage::Role::Assistant;
            if (m_TurnActive && !assistantBubbleLive)
                thinkingIndicator();

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
        m_TurnActive = false;
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
