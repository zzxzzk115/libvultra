#pragma once

#include "editor_app/agent/agent_backend.hpp"
#include "editor_app/ui/editor_window.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct ImVec2;

namespace vultra_app
{
    // In-editor chat panel. Owns an IAgentBackend (ClaudeCliBackend in v1) which runs the
    // agent CLI as a child process; the panel renders the streamed conversation and forwards
    // user turns. All ImGui/state mutation happens on the main thread; the backend's reader
    // thread only feeds an event queue that tick() drains.
    class AiChatWindow final : public EditorWindow
    {
    public:
        AiChatWindow();

        void tick(EditorContext& ctx) override;
        void draw(EditorContext& ctx) override;
        void onClosed(EditorContext& ctx) override;
        void onDestroy(EditorContext& ctx) override;

    private:
        enum class Status
        {
            NotStarted,
            Ready,
            Error,
        };

        struct ToolInvocation
        {
            enum class State
            {
                Running,
                Done,
                Error,
            };
            std::string    id;
            std::string    name;
            nlohmann::json input;
            std::string    result;
            State          state {State::Running};
        };

        struct ChatMessage
        {
            enum class Role
            {
                User,
                Assistant,
                System,
            };
            Role                        role {Role::Assistant};
            std::string                 text;
            std::string                 thinking;
            std::vector<ToolInvocation> tools;
            bool                        streaming {false};
            // Typewriter reveal: how many bytes of `text` are currently shown. Advances toward
            // text.size() each frame so streamed replies type out instead of popping in whole.
            double                      revealed {0.0};
        };

        bool ensureBackend(EditorContext& ctx); // lazy spawn; false + sets m_Status on failure
        void submitMessage(EditorContext& ctx, std::string text); // spawn if needed + send a user turn
        void clearConversation();
        ChatMessage& currentAssistantMessage(); // streaming assistant message, creating if needed

        void drawStatusBanner(EditorContext& ctx);
        void drawMessage(const ChatMessage& message, std::size_t index);
        void drawToolCard(const ToolInvocation& tool);
        void drawComposer(EditorContext& ctx);
        void drawPermissionPicker(EditorContext& ctx, const ImVec2& size); // popup left of Send
        void advanceReveal();                                        // typewriter tick

        std::unique_ptr<agent::IAgentBackend> m_Backend;
        std::vector<ChatMessage>              m_Messages;
        std::array<char, 8192>                m_InputBuffer {};
        Status                                m_Status {Status::NotStarted};
        std::string                           m_StatusDetail;
        bool                                  m_AutoScroll {true};
        bool                                  m_RequestScrollToBottom {false};
        // True from the moment a turn is sent until the agent reports the turn finished (or a fatal
        // error). Drives the Send/Stop toggle and the "Thinking" indicator; intermediate events
        // (text deltas, tool calls, diagnostics) must NOT clear it.
        bool                                  m_TurnActive {false};
        bool                                  m_AutoPromptChecked {false}; // VULTRA_AI_CHAT_PROMPT seeded once
        // Claude permission mode for the session: "default" | "acceptEdits" | "plan" | "bypassPermissions".
        // Chosen via the composer popup; applied on backend start and pushed live when it changes.
        std::string                           m_PermissionMode {"acceptEdits"};
    };
} // namespace vultra_app
