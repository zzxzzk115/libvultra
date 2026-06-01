#include "editor_app/runtime_mcp_server.hpp"
#include "editor_app/runtime_mcp_server_internal.hpp"

#include <vultra/function/services/render_backend_service.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <thread>
#include <vector>

namespace vultra_app
{
    namespace
    {        int closePipe(FILE* pipe)
        {
            if (!pipe)
                return 0;
#if defined(_WIN32)
            return _pclose(pipe);
#else
            return pclose(pipe);
#endif
        }
    } // namespace

    void RuntimeMcpServer::startRecordingWriter()
    {
        {
            std::lock_guard lock {m_RecordingWriterMutex};
            m_RecordingFrameQueue.clear();
            m_RecordingWriterStop     = false;
            m_RecordingWriterExitCode = 0;
        }
        m_RecordingWriterThread = std::thread([this] {
            while (true)
            {
                std::vector<uint8_t> frame;
                {
                    std::unique_lock lock {m_RecordingWriterMutex};
                    m_RecordingWriterCv.wait(lock, [this] {
                        return m_RecordingWriterStop || !m_RecordingFrameQueue.empty();
                    });
                    if (m_RecordingFrameQueue.empty())
                    {
                        if (m_RecordingWriterStop)
                            break;
                        continue;
                    }
                    frame = std::move(m_RecordingFrameQueue.front());
                    m_RecordingFrameQueue.pop_front();
                    m_Recording.queuedFrames = static_cast<uint32_t>(m_RecordingFrameQueue.size());
                }

                if (m_RecordingPipe)
                {
                    const auto written = std::fwrite(frame.data(), 1, frame.size(), m_RecordingPipe);
                    if (written != frame.size())
                    {
                        std::lock_guard lock {m_RecordingWriterMutex};
                        ++m_Recording.droppedFrames;
                    }
                }
            }
        });
    }

    int RuntimeMcpServer::stopRecordingWriter()
    {
        {
            std::lock_guard lock {m_RecordingWriterMutex};
            m_RecordingWriterStop = true;
        }
        m_RecordingWriterCv.notify_all();
        if (m_RecordingWriterThread.joinable())
            m_RecordingWriterThread.join();
        m_RecordingWriterExitCode = closePipe(m_RecordingPipe);
        m_RecordingPipe = nullptr;
        {
            std::lock_guard lock {m_RecordingWriterMutex};
            m_RecordingFrameQueue.clear();
            m_Recording.queuedFrames = 0;
        }
        return m_RecordingWriterExitCode;
    }

    void RuntimeMcpServer::enqueueRecordingFrame(std::vector<uint8_t> frameBytes)
    {
        {
            std::lock_guard lock {m_RecordingWriterMutex};
            const auto maxQueued = std::max<uint32_t>(m_Recording.maxQueuedFrames, 1u);
            if (m_RecordingFrameQueue.size() >= maxQueued)
            {
                ++m_Recording.droppedFrames;
                return;
            }
            m_RecordingFrameQueue.push_back(std::move(frameBytes));
            m_Recording.queuedFrames = static_cast<uint32_t>(m_RecordingFrameQueue.size());
        }
        m_RecordingWriterCv.notify_one();
    }


    void RuntimeMcpServer::captureRecordingFrame(EditorContext& ctx)
    {
        if (!m_Recording.active)
            return;
        if (m_Recording.maxFrames > 0 && m_Recording.frameCount >= m_Recording.maxFrames)
        {
            m_Recording.active = false;
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto interval =
            std::chrono::duration<double>(1.0 / static_cast<double>(std::max(m_Recording.fps, 1)));
        if (m_Recording.frameCount > 0 && now - m_Recording.lastFrameAt < interval)
            return;

        auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
        if (!backendService)
        {
            m_Recording.lastError = "render backend service is unavailable";
            return;
        }

        char filename[64] {};
        auto&      backbuffer = backendService->backbuffer();
        if (m_Recording.mode == "stream")
        {
            if (!m_RecordingPipe)
            {
                m_Recording.lastError = "ffmpeg stream pipe is not open";
                return;
            }
            auto bytes = backendService->renderDevice().readTextureBytes(backbuffer);
            if (!bytes)
            {
                m_Recording.lastError = "texture readback failed for recording stream";
                return;
            }
            enqueueRecordingFrame(std::move(*bytes));
            m_Recording.lastFrameFile = {};
        }
        else
        {
            std::snprintf(filename, sizeof(filename), "frame_%06u.png", m_Recording.frameCount);
            const auto frameFile = (std::filesystem::path(m_Recording.frameDirectory) / filename).lexically_normal();
            std::error_code ec;
            std::filesystem::create_directories(frameFile.parent_path(), ec);
            if (ec)
            {
                m_Recording.lastError = "failed to create recording frame directory: " + ec.message();
                return;
            }

            const bool saved = backendService->renderDevice().saveTextureToFile(backbuffer, frameFile.generic_string());
            if (!saved)
            {
                m_Recording.lastError = "failed to save recording frame: " + frameFile.generic_string();
                return;
            }
            m_Recording.lastFrameFile = frameFile.generic_string();
        }

        m_Recording.lastError.clear();
        ++m_Recording.frameCount;
        m_Recording.lastFrameAt = now;
        if (m_Recording.maxFrames > 0 && m_Recording.frameCount >= m_Recording.maxFrames)
            m_Recording.active = false;
    }

} // namespace vultra_app