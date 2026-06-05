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
    // Single source of truth for an MCP tool-call deadline. The HTTP worker stops waiting for a
    // result after this, and the main thread must likewise stop running/mutating on behalf of a
    // (possibly deferred) call once the same deadline passes - otherwise a client that already gave
    // up at the timeout could still observe orphaned scene/asset mutations, and a deferred condition
    // that never resolves would spin every frame forever.
    inline constexpr auto kMcpToolDeadline = std::chrono::seconds(5);

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
        bool simStepRequested {false};
        bool simActionsApplied {false};
        uint32_t simFramesRemaining {0};
        uint64_t frameCaptureStartFrame {0};
        uint32_t frameCapturePolls {0};
        std::chrono::steady_clock::time_point frameCaptureStartedAt {};
        // Absolute deadline shared with the HTTP worker's wait. Set once when the call is enqueued
        // so that re-deferring across frames never extends it.
        std::chrono::steady_clock::time_point deferDeadline {};
        std::string frameCaptureCameraName;
    };
} // namespace vultra_app
