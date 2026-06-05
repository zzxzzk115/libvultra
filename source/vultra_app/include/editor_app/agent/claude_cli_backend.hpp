#pragma once

#include "editor_app/agent/agent_backend.hpp"
#include "editor_app/agent/subprocess.hpp"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace vultra_app::agent
{
    // Drives the Claude CLI as a persistent child in stream-json mode:
    //   claude -p --input-format stream-json --output-format stream-json --verbose [...]
    // One user turn == one NDJSON line on stdin; the child keeps conversation state for the
    // life of the process. A reader thread parses stdout NDJSON into AgentEvents.
    class ClaudeCliBackend final : public IAgentBackend
    {
    public:
        ClaudeCliBackend() = default;
        ~ClaudeCliBackend() override;

        bool                    start(const AgentBackendConfig& config, std::string* error) override;
        void                    sendUserMessage(std::string text) override;
        std::vector<AgentEvent> drainEvents() override;
        void                    interrupt() override;
        void                    shutdown() override;
        [[nodiscard]] bool      isReady() const override;

    private:
        void readerLoop(std::stop_token stop);
        void mapStreamJsonLine(const std::string& line); // reader thread only
        void pushEvent(AgentEvent ev);

        Subprocess             m_Proc;
        std::jthread           m_Reader;
        std::mutex             m_Mutex;
        std::deque<AgentEvent> m_Events;
        std::atomic<bool>      m_Ready {false};
    };
} // namespace vultra_app::agent
