#pragma once

#include "editor_app/editor_context.hpp"

#include <vultra/core/rhi/buffer.hpp>
#include <vultra/core/rhi/command_buffer.hpp>
#include <vultra/core/rhi/structs/pixel_format.hpp>
#include <vultra/core/rhi/texture.hpp>

#include <nlohmann/json_fwd.hpp>

#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace vultra_app
{
    class RuntimeMcpServer
    {
    public:
        RuntimeMcpServer() = default;
        ~RuntimeMcpServer();

        RuntimeMcpServer(const RuntimeMcpServer&)            = delete;
        RuntimeMcpServer& operator=(const RuntimeMcpServer&) = delete;
        RuntimeMcpServer(RuntimeMcpServer&&)                 = delete;
        RuntimeMcpServer& operator=(RuntimeMcpServer&&)      = delete;

        bool start(std::string host, uint16_t port, std::string* error = nullptr);
        void stop();
        void syncDesiredState(EditorContext& ctx);
        void executePending(EditorContext& ctx);

        [[nodiscard]] bool isRunning() const;
        [[nodiscard]] bool matchesDesiredEndpoint(const std::string& host, uint16_t port) const;
        [[nodiscard]] std::string endpoint() const;
        [[nodiscard]] std::string lastError() const;

    private:
        struct PendingCall;
        struct VideoStreamRawFrame;

        void serverLoop();
        void enqueueCall(std::shared_ptr<PendingCall> call);

        [[nodiscard]] std::string handleHttpRequest(std::string_view requestText);
        [[nodiscard]] nlohmann::json handleMcpRequest(const nlohmann::json& request);
        [[nodiscard]] nlohmann::json handleToolCallOnMainThread(std::string_view name,
                                                                 const nlohmann::json& args,
                                                                 EditorContext& ctx,
                                                                 PendingCall* call);
        [[nodiscard]] nlohmann::json handleAssetTool(std::string_view name,
                                                      const nlohmann::json& args,
                                                      EditorContext& ctx);
        [[nodiscard]] nlohmann::json handleEditorAutomationTool(std::string_view name,
                                                                const nlohmann::json& args,
                                                                EditorContext& ctx);
        [[nodiscard]] nlohmann::json handleRuntimeTool(std::string_view name,
                                                       const nlohmann::json& args,
                                                       EditorContext& ctx,
                                                       PendingCall* call);
        [[nodiscard]] nlohmann::json handleSimTool(std::string_view name,
                                                   const nlohmann::json& args,
                                                   EditorContext& ctx,
                                                   PendingCall* call);
        void captureRecordingFrame(EditorContext& ctx);
        void captureVideoStreamFrame(EditorContext& ctx);
        void startRecordingWriter();
        int  stopRecordingWriter();
        void enqueueRecordingFrame(std::vector<uint8_t> frameBytes);
        void stopVideoStreamClients();
        void startVideoStreamEncoder();
        void stopVideoStreamEncoder();
        void enqueueVideoStreamFrame(VideoStreamRawFrame frame);
        void pollVideoStreamReadbacks();
        void clearVideoStreamReadbacks();

        mutable std::mutex m_Mutex;
        std::condition_variable m_StateCv;
        std::queue<std::shared_ptr<PendingCall>> m_PendingCalls;
        std::vector<std::shared_ptr<PendingCall>> m_DeferredCalls;
        std::thread m_ServerThread;
        std::string m_Host {"127.0.0.1"};
        std::string m_LastError;
        uint16_t    m_RequestedPort {8848};
        uint16_t    m_Port {8848};
        bool        m_Running {false};
        bool        m_Listening {false};
        bool        m_StopRequested {false};

        struct RecordingState
        {
            bool        active {false};
            bool        encodeOnStop {true};
            int         fps {30};
            uint32_t    maxFrames {0};
            uint32_t    frameCount {0};
            std::string outputFile;
            std::string frameDirectory;
            std::string mode {"stream"};
            std::string streamPixelFormat;
            std::chrono::steady_clock::time_point startedAt {};
            std::chrono::steady_clock::time_point lastFrameAt {};
            std::string lastFrameFile;
            std::string lastError;
            std::string ffmpegCommand;
            uint32_t    width {0};
            uint32_t    height {0};
            uint32_t    droppedFrames {0};
            uint32_t    queuedFrames {0};
            uint32_t    maxQueuedFrames {3};
        };
        RecordingState m_Recording;
        std::thread m_RecordingWriterThread;
        std::mutex m_RecordingWriterMutex;
        std::condition_variable m_RecordingWriterCv;
        std::deque<std::vector<uint8_t>> m_RecordingFrameQueue;
        FILE* m_RecordingPipe {nullptr};
        bool  m_RecordingWriterStop {false};
        int   m_RecordingWriterExitCode {0};

        struct VideoStreamState
        {
            bool        active {false};
            std::string id;
            int         fps {0};
            int         jpegQuality {80};
            uint32_t    frameCount {0};
            uint32_t    capturedFrameCount {0};
            uint32_t    readbackSubmittedCount {0};
            uint32_t    readbackCompletedCount {0};
            uint32_t    readbackSkippedCount {0};
            uint32_t    droppedFrames {0};
            uint32_t    queuedFrames {0};
            uint32_t    width {0};
            uint32_t    height {0};
            uint32_t    sourceWidth {0};
            uint32_t    sourceHeight {0};
            uint32_t    maxWidth {0};
            uint32_t    maxHeight {0};
            uint64_t    sequence {0};
            std::vector<uint8_t> latestJpeg;
            std::chrono::steady_clock::time_point startedAt {};
            std::chrono::steady_clock::time_point lastFrameAt {};
            std::chrono::steady_clock::time_point lastCaptureAt {};
            std::string lastError;
        };
        struct VideoStreamRawFrame
        {
            std::vector<uint8_t> bytes;
            vultra::rhi::PixelFormat format {vultra::rhi::PixelFormat::eUndefined};
            uint32_t width {0};
            uint32_t height {0};
            uint32_t sourceWidth {0};
            uint32_t sourceHeight {0};
            uint64_t captureSequence {0};
        };
        struct VideoStreamReadbackSlot
        {
            vultra::rhi::CommandBuffer commandBuffer;
            vultra::rhi::Buffer readbackBuffer;
            vultra::rhi::Texture readbackTexture;
            vultra::rhi::PixelFormat format {vultra::rhi::PixelFormat::eUndefined};
            uint32_t width {0};
            uint32_t height {0};
            uint32_t sourceWidth {0};
            uint32_t sourceHeight {0};
            uint64_t captureSequence {0};
            bool pending {false};
        };
        VideoStreamState                      m_VideoStream;
        std::mutex                            m_VideoStreamMutex;
        std::condition_variable               m_VideoStreamCv;
        std::mutex                            m_VideoStreamClientMutex;
        std::vector<std::thread>              m_VideoStreamClientThreads;
        std::thread                           m_VideoStreamEncoderThread;
        std::mutex                            m_VideoStreamEncoderMutex;
        std::condition_variable               m_VideoStreamEncoderCv;
        std::deque<VideoStreamRawFrame>       m_VideoStreamRawFrames;
        std::vector<VideoStreamReadbackSlot>  m_VideoStreamReadbackSlots;
        bool                                  m_VideoStreamEncoderStop {false};
    };
} // namespace vultra_app
