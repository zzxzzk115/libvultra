#pragma once

#include "editor_app/runtime_mcp_server.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

namespace vultra_app
{
    struct RuntimeMcpServer::PendingCall
    {
        std::string name;
        nlohmann::json args;
        nlohmann::json result;
        std::mutex mutex;
        std::condition_variable cv;
        bool done {false};
        bool defer {false};
        bool frameCaptureRequested {false};
        uint64_t frameCaptureStartFrame {0};
        uint32_t frameCapturePolls {0};
        std::chrono::steady_clock::time_point frameCaptureStartedAt {};
        std::string frameCaptureCameraName;
    };
} // namespace vultra_app