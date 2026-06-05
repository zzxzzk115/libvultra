#pragma once

#include "editor_app/agent/agent_backend.hpp"
#include "editor_app/ui/editor_window.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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
        };

        bool ensureBackend(EditorContext& ctx); // lazy spawn; false + sets m_Status on failure
        void clearConversation();
        ChatMessage& currentAssistantMessage(); // streaming assistant message, creating if needed

        void drawStatusBanner(EditorContext& ctx);
        void drawMessage(const ChatMessage& message);
        void drawToolCard(const ToolInvocation& tool);
        void drawComposer(EditorContext& ctx);

        std::unique_ptr<agent::IAgentBackend> m_Backend;
        std::filesystem::path                 m_McpConfigPath; // temp file, deleted on shutdown
        std::vector<ChatMessage>              m_Messages;
        std::array<char, 8192>                m_InputBuffer {};
        Status                                m_Status {Status::NotStarted};
        std::string                           m_StatusDetail;
        bool                                  m_AutoScroll {true};
        bool                                  m_RequestScrollToBottom {false};
        bool                                  m_AwaitingReply {false};
    };
} // namespace vultra_app
