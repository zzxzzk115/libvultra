#include "editor_app/runtime_mcp_server.hpp"
#include "editor_app/runtime_mcp_server_internal.hpp"

#include <vultra/core/rhi/structs/pixel_format.hpp>
#include <vultra/function/services/render_backend_service.hpp>

#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace vultra_app
{
    namespace
    {
        int closePipe(FILE* pipe)
        {
            if (!pipe)
                return 0;
#if defined(_WIN32)
            return _pclose(pipe);
#else
            return pclose(pipe);
#endif
        }

        void appendJpegBytes(void* context, void* data, const int size)
        {
            if (!context || !data || size <= 0)
                return;
            auto& out = *static_cast<std::vector<uint8_t>*>(context);
            const auto* bytes = static_cast<const uint8_t*>(data);
            out.insert(out.end(), bytes, bytes + size);
        }

        std::pair<uint32_t, uint32_t> fittedExtent(const uint32_t sourceWidth,
                                                   const uint32_t sourceHeight,
                                                   const uint32_t maxWidth,
                                                   const uint32_t maxHeight)
        {
            if (sourceWidth == 0u || sourceHeight == 0u)
                return {0u, 0u};
            if (maxWidth == 0u && maxHeight == 0u)
                return {sourceWidth, sourceHeight};

            const uint32_t boundedMaxWidth = maxWidth == 0u ? sourceWidth : maxWidth;
            const uint32_t boundedMaxHeight = maxHeight == 0u ? sourceHeight : maxHeight;
            if (sourceWidth <= boundedMaxWidth && sourceHeight <= boundedMaxHeight)
                return {sourceWidth, sourceHeight};

            const double sx = static_cast<double>(boundedMaxWidth) / static_cast<double>(sourceWidth);
            const double sy = static_cast<double>(boundedMaxHeight) / static_cast<double>(sourceHeight);
            const double s = std::max(0.001, std::min(sx, sy));
            return {
                std::max(1u, static_cast<uint32_t>(static_cast<double>(sourceWidth) * s)),
                std::max(1u, static_cast<uint32_t>(static_cast<double>(sourceHeight) * s)),
            };
        }

        std::optional<std::vector<uint8_t>> toRgb8(const std::vector<uint8_t>& bytes,
                                                   const vultra::rhi::PixelFormat format,
                                                   const uint32_t sourceWidth,
                                                   const uint32_t sourceHeight,
                                                   const uint32_t targetWidth,
                                                   const uint32_t targetHeight)
        {
            if (format != vultra::rhi::PixelFormat::eBGRA8_UNorm &&
                format != vultra::rhi::PixelFormat::eRGBA8_UNorm)
            {
                return std::nullopt;
            }
            if (sourceWidth == 0u || sourceHeight == 0u || targetWidth == 0u || targetHeight == 0u)
                return std::nullopt;
            if (bytes.size() < static_cast<size_t>(sourceWidth) * static_cast<size_t>(sourceHeight) * 4u)
                return std::nullopt;

            std::vector<uint8_t> rgb;
            rgb.resize(static_cast<size_t>(targetWidth) * static_cast<size_t>(targetHeight) * 3u);
            for (uint32_t y = 0u; y < targetHeight; ++y)
            {
                const uint32_t srcY = std::min(sourceHeight - 1u,
                                               static_cast<uint32_t>((static_cast<uint64_t>(y) * sourceHeight) /
                                                                     targetHeight));
                for (uint32_t x = 0u; x < targetWidth; ++x)
                {
                    const uint32_t srcX = std::min(sourceWidth - 1u,
                                                   static_cast<uint32_t>((static_cast<uint64_t>(x) * sourceWidth) /
                                                                         targetWidth));
                    const size_t src = (static_cast<size_t>(srcY) * sourceWidth + srcX) * 4u;
                    const size_t dst = (static_cast<size_t>(y) * targetWidth + x) * 3u;
                    if (format == vultra::rhi::PixelFormat::eBGRA8_UNorm)
                    {
                        rgb[dst + 0u] = bytes[src + 2u];
                        rgb[dst + 1u] = bytes[src + 1u];
                        rgb[dst + 2u] = bytes[src + 0u];
                    }
                    else
                    {
                        rgb[dst + 0u] = bytes[src + 0u];
                        rgb[dst + 1u] = bytes[src + 1u];
                        rgb[dst + 2u] = bytes[src + 2u];
                    }
                }
            }
            return rgb;
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

    void RuntimeMcpServer::stopVideoStreamClients()
    {
        {
            std::lock_guard lock {m_VideoStreamMutex};
            m_VideoStream.active = false;
        }
        m_VideoStreamCv.notify_all();
        stopVideoStreamEncoder();
        clearVideoStreamReadbacks();

        std::vector<std::thread> threads;
        {
            std::lock_guard lock {m_VideoStreamClientMutex};
            threads.swap(m_VideoStreamClientThreads);
        }
        for (auto& thread : threads)
        {
            if (thread.joinable())
                thread.join();
        }
    }

    void RuntimeMcpServer::startVideoStreamEncoder()
    {
        stopVideoStreamEncoder();
        {
            std::lock_guard lock {m_VideoStreamEncoderMutex};
            m_VideoStreamRawFrames.clear();
            m_VideoStreamEncoderStop = false;
        }
        m_VideoStreamEncoderThread = std::thread([this] {
            while (true)
            {
                VideoStreamRawFrame frame;
                {
                    std::unique_lock lock {m_VideoStreamEncoderMutex};
                    m_VideoStreamEncoderCv.wait(lock, [this] {
                        return m_VideoStreamEncoderStop || !m_VideoStreamRawFrames.empty();
                    });
                    if (m_VideoStreamRawFrames.empty())
                    {
                        if (m_VideoStreamEncoderStop)
                            break;
                        continue;
                    }
                    frame = std::move(m_VideoStreamRawFrames.back());
                    m_VideoStreamRawFrames.clear();
                    {
                        std::lock_guard stateLock {m_VideoStreamMutex};
                        m_VideoStream.queuedFrames = 0;
                    }
                }

                int quality = 80;
                {
                    std::lock_guard lock {m_VideoStreamMutex};
                    if (!m_VideoStream.active)
                        continue;
                    quality = std::clamp(m_VideoStream.jpegQuality, 1, 100);
                }

                auto rgb = toRgb8(frame.bytes, frame.format, frame.width, frame.height, frame.width, frame.height);
                if (!rgb)
                {
                    {
                        std::lock_guard lock {m_VideoStreamMutex};
                        m_VideoStream.lastError = "video stream only supports RGBA8/BGRA8 backbuffers";
                    }
                    continue;
                }

                std::vector<uint8_t> jpeg;
                const int ok = stbi_write_jpg_to_func(appendJpegBytes,
                                                       &jpeg,
                                                       static_cast<int>(frame.width),
                                                       static_cast<int>(frame.height),
                                                       3,
                                                       rgb->data(),
                                                       quality);
                if (ok == 0 || jpeg.empty())
                {
                    {
                        std::lock_guard lock {m_VideoStreamMutex};
                        m_VideoStream.lastError = "failed to encode video stream JPEG frame";
                    }
                    continue;
                }

                {
                    std::lock_guard lock {m_VideoStreamMutex};
                    if (!m_VideoStream.active)
                        continue;
                    m_VideoStream.sourceWidth = frame.sourceWidth;
                    m_VideoStream.sourceHeight = frame.sourceHeight;
                    m_VideoStream.width = frame.width;
                    m_VideoStream.height = frame.height;
                    m_VideoStream.latestJpeg = std::move(jpeg);
                    m_VideoStream.lastFrameAt = std::chrono::steady_clock::now();
                    m_VideoStream.lastError.clear();
                    ++m_VideoStream.frameCount;
                    ++m_VideoStream.sequence;
                }
                m_VideoStreamCv.notify_all();
            }
        });
    }

    void RuntimeMcpServer::stopVideoStreamEncoder()
    {
        {
            std::lock_guard lock {m_VideoStreamEncoderMutex};
            m_VideoStreamEncoderStop = true;
            m_VideoStreamRawFrames.clear();
        }
        {
            std::lock_guard lock {m_VideoStreamMutex};
            m_VideoStream.queuedFrames = 0;
        }
        m_VideoStreamEncoderCv.notify_all();
        if (m_VideoStreamEncoderThread.joinable())
            m_VideoStreamEncoderThread.join();
    }

    void RuntimeMcpServer::clearVideoStreamReadbacks()
    {
        m_VideoStreamReadbackSlots.clear();
    }

    void RuntimeMcpServer::pollVideoStreamReadbacks()
    {
        if (m_VideoStreamReadbackSlots.empty())
            return;

        for (auto& slot : m_VideoStreamReadbackSlots)
        {
            if (!slot.pending || !slot.commandBuffer.isComplete())
                continue;

            auto* mappedPtr = static_cast<const uint8_t*>(slot.readbackBuffer.map());
            if (!mappedPtr)
            {
                std::lock_guard lock {m_VideoStreamMutex};
                m_VideoStream.lastError = "failed to map video stream readback buffer";
                slot.commandBuffer.reset();
                slot.pending = false;
                continue;
            }

            std::vector<uint8_t> bytes(slot.readbackBuffer.getSize());
            std::memcpy(bytes.data(), mappedPtr, bytes.size());
            slot.readbackBuffer.unmap();
            slot.commandBuffer.reset();

            {
                std::lock_guard lock {m_VideoStreamMutex};
                if (!m_VideoStream.active)
                {
                    slot.pending = false;
                    continue;
                }
                m_VideoStream.sourceWidth = slot.sourceWidth;
                m_VideoStream.sourceHeight = slot.sourceHeight;
                m_VideoStream.lastError.clear();
                ++m_VideoStream.capturedFrameCount;
                ++m_VideoStream.readbackCompletedCount;
            }

            enqueueVideoStreamFrame(VideoStreamRawFrame {
                .bytes = std::move(bytes),
                .format = slot.format,
                .width = slot.width,
                .height = slot.height,
                .sourceWidth = slot.sourceWidth,
                .sourceHeight = slot.sourceHeight,
                .captureSequence = slot.captureSequence,
            });
            slot.pending = false;
        }
    }

    void RuntimeMcpServer::enqueueVideoStreamFrame(VideoStreamRawFrame frame)
    {
        {
            std::lock_guard lock {m_VideoStreamEncoderMutex};
            if (!m_VideoStreamRawFrames.empty())
            {
                std::lock_guard stateLock {m_VideoStreamMutex};
                m_VideoStream.droppedFrames += static_cast<uint32_t>(m_VideoStreamRawFrames.size());
                m_VideoStreamRawFrames.clear();
            }
            m_VideoStreamRawFrames.push_back(std::move(frame));
            {
                std::lock_guard stateLock {m_VideoStreamMutex};
                m_VideoStream.queuedFrames = static_cast<uint32_t>(m_VideoStreamRawFrames.size());
            }
        }
        m_VideoStreamEncoderCv.notify_one();
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

    void RuntimeMcpServer::captureVideoStreamFrame(EditorContext& ctx)
    {
        pollVideoStreamReadbacks();

        {
            std::lock_guard lock {m_VideoStreamMutex};
            if (!m_VideoStream.active)
                return;

            const auto now = std::chrono::steady_clock::now();
            if (m_VideoStream.fps > 0)
            {
                const auto interval =
                    std::chrono::duration<double>(1.0 / static_cast<double>(std::max(m_VideoStream.fps, 1)));
                if (m_VideoStream.readbackSubmittedCount > 0 && now - m_VideoStream.lastCaptureAt < interval)
                    return;
            }
        }

        auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
        if (!backendService)
        {
            std::lock_guard lock {m_VideoStreamMutex};
            m_VideoStream.lastError = "render backend service is unavailable";
            return;
        }

        auto& backbuffer = backendService->backbuffer();
        auto& rd = backendService->renderDevice();
        const auto sourceExtent = backbuffer.getExtent();
        uint32_t maxWidth = 0u;
        uint32_t maxHeight = 0u;
        {
            std::lock_guard lock {m_VideoStreamMutex};
            maxWidth = m_VideoStream.maxWidth;
            maxHeight = m_VideoStream.maxHeight;
        }
        const auto [targetWidth, targetHeight] =
            fittedExtent(sourceExtent.width, sourceExtent.height, maxWidth, maxHeight);
        const bool scaledReadback = targetWidth != sourceExtent.width || targetHeight != sourceExtent.height;
        const auto readbackSize =
            static_cast<uint64_t>(targetWidth) * static_cast<uint64_t>(targetHeight) *
            static_cast<uint64_t>(vultra::rhi::getBytesPerPixel(backbuffer.getPixelFormat()));

        auto slotIt = std::find_if(m_VideoStreamReadbackSlots.begin(), m_VideoStreamReadbackSlots.end(), [](const auto& slot) {
            return !slot.pending;
        });
        if (slotIt == m_VideoStreamReadbackSlots.end())
        {
            if (m_VideoStreamReadbackSlots.size() < 3u)
            {
                m_VideoStreamReadbackSlots.emplace_back();
                slotIt = std::prev(m_VideoStreamReadbackSlots.end());
            }
            else
            {
                std::lock_guard lock {m_VideoStreamMutex};
                ++m_VideoStream.readbackSkippedCount;
                return;
            }
        }

        if (!slotIt->readbackBuffer || slotIt->readbackBuffer.getSize() != readbackSize)
            slotIt->readbackBuffer = rd.createReadbackBuffer(readbackSize);
        if (scaledReadback &&
            (!slotIt->readbackTexture || slotIt->readbackTexture.getExtent().width != targetWidth ||
             slotIt->readbackTexture.getExtent().height != targetHeight ||
             slotIt->readbackTexture.getPixelFormat() != backbuffer.getPixelFormat()))
        {
            slotIt->readbackTexture =
                rd.createTexture2D({targetWidth, targetHeight},
                                   backbuffer.getPixelFormat(),
                                   1u,
                                   0u,
                                   vultra::rhi::ImageUsage::eTransfer);
        }
        if (!slotIt->commandBuffer)
            slotIt->commandBuffer = rd.createCommandBuffer();

        uint64_t captureSequence = 0u;
        {
            std::lock_guard lock {m_VideoStreamMutex};
            if (!m_VideoStream.active)
                return;
            ++m_VideoStream.readbackSubmittedCount;
            captureSequence = m_VideoStream.readbackSubmittedCount;
            m_VideoStream.sourceWidth = sourceExtent.width;
            m_VideoStream.sourceHeight = sourceExtent.height;
            m_VideoStream.lastCaptureAt = std::chrono::steady_clock::now();
            m_VideoStream.lastError.clear();
        }

        slotIt->format = backbuffer.getPixelFormat();
        slotIt->width = targetWidth;
        slotIt->height = targetHeight;
        slotIt->sourceWidth = sourceExtent.width;
        slotIt->sourceHeight = sourceExtent.height;
        slotIt->captureSequence = captureSequence;
        slotIt->pending = true;

        auto& cb = slotIt->commandBuffer;
        cb.begin();
        if (scaledReadback)
        {
            cb.blit(backbuffer, slotIt->readbackTexture, vultra::rhi::TexelFilter::eLinear);
            cb.getBarrierBuilder().imageBarrier(
                {
                    .image = slotIt->readbackTexture,
                    .newLayout = vultra::rhi::ImageLayout::eGeneral,
                },
                {
                    .dstStage = vultra::rhi::PipelineStages::eTransfer,
                    .dstAccess = vultra::rhi::Access::eTransferRead,
                });
            cb.copyImage(slotIt->readbackTexture, slotIt->readbackBuffer, vultra::rhi::ImageAspect::eColor);
        }
        else
        {
            cb.getBarrierBuilder().imageBarrier(
                {
                    .image = backbuffer,
                    .newLayout = vultra::rhi::ImageLayout::eGeneral,
                },
                {
                    .dstStage = vultra::rhi::PipelineStages::eTransfer,
                    .dstAccess = vultra::rhi::Access::eTransferRead,
                });
            cb.copyImage(backbuffer, slotIt->readbackBuffer, vultra::rhi::ImageAspect::eColor);
        }
        cb.getBarrierBuilder().bufferBarrier({.buffer = slotIt->readbackBuffer},
                                             {
                                                 .dstStage = vultra::rhi::PipelineStages::eTransfer,
                                                 .dstAccess = vultra::rhi::Access::eTransferRead,
                                             });
        rd.execute(cb, vultra::rhi::JobInfo {}, false);
    }

} // namespace vultra_app
