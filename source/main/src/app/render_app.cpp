#include <vultra/main/app/render_app.hpp>

#include <chrono>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        bool copyToSwapchain(Device& device, Frame& frame, Swapchain& swapchain, Texture& source)
        {
            // Browser canvas images expire on an event-loop yield. Acquire only after rendering/readback
            // has finished, then record and submit the copy before the next GPU wait can yield.
            auto* canvas = swapchain.acquire();
            if (!canvas)
            {
                return false;
            }
            auto* cmd = frame.begin();
            source.transition(cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            canvas->transition(cmd,
                               {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            VriTextureCopyDesc copy {};
            copy.src.layerNum = 1;
            copy.dst.layerNum = 1;
            copy.src.aspect   = VriImageAspect_Color;
            copy.dst.aspect   = VriImageAspect_Color;
            device.core.CmdCopyTexture(cmd, canvas->handle, source.handle, &copy);
            canvas->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
            frame.submitAndWait();
            return true;
        }
    } // namespace

    RenderApp::RenderApp(const AppConfig& config)
#ifdef VULTRA_WITH_RENDERDOC
        :
        m_RenderDocFrame(config.renderDocFrame),
        m_RenderDoc(config.renderDocFrame ? std::make_unique<RenderDocCapture>() : nullptr)
#endif
    {
#ifndef VULTRA_WITH_RENDERDOC
        if (config.renderDocFrame)
        {
            throw std::invalid_argument("This platform build does not provide RenderDoc capture");
        }
#endif
    }

    std::optional<std::string> RenderApp::advanceFrame(Device&    device,
                                                       Swapchain& swapchain,
                                                       Frame&     frame,
                                                       Profiler&  profiler,
                                                       float      deltaSeconds,
                                                       bool       renderable,
                                                       Texture*   presentSource)
    {
        const auto frameStart = std::chrono::steady_clock::now();
        onPreUpdate(deltaSeconds);
        onUpdate(deltaSeconds);
        onPostUpdate(deltaSeconds);
        if (m_CloseRequested)
        {
            return std::nullopt;
        }
        if (!renderable)
        {
            onRenderSkipped();
            return std::nullopt;
        }
        const auto afterUpdate = std::chrono::steady_clock::now();

        auto* target = presentSource ? presentSource : swapchain.acquire();
        if (!target)
        {
            onRenderSkipped();
            return std::nullopt;
        }
        const auto afterAcquire = std::chrono::steady_clock::now();
#ifdef VULTRA_WITH_RENDERDOC
        if (m_RenderDocFrame == m_FrameCount)
        {
            m_RenderDoc->begin();
        }
#endif
        if (m_PreviousSize != swapchain.size())
        {
            m_PreviousSize = swapchain.size();
            onResize(m_PreviousSize);
        }
        // Once acquired, finish and present this frame even if a render callback calls close().
        onPreRender();
        auto* cmd = frame.begin();
        device.core.CmdBeginDebugGroup(cmd, "Frame");
        profiler.beginFrame(cmd);
        profiler.beginPass(cmd, "Frame");
        onRender(cmd, *target);
        if (!presentSource)
        {
            target->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
        }
        profiler.endPass(cmd);
        profiler.resolve(cmd);
        device.core.CmdEndDebugGroup(cmd);
        const auto afterRecord = std::chrono::steady_clock::now();
        frame.submitAndWait();
        const auto afterWait = std::chrono::steady_clock::now();
        profiler.collect();
        onPostRender(*target);
        const auto afterPostRender = std::chrono::steady_clock::now();
        if (presentSource && !copyToSwapchain(device, frame, swapchain, *presentSource))
        {
            onRenderSkipped();
            return std::nullopt;
        }
        swapchain.present();
        onPostPresent();
#ifdef VULTRA_WITH_RENDERDOC
        if (m_RenderDocFrame == m_FrameCount)
        {
            m_RenderDoc->end();
        }
#endif
        const auto afterPresent = std::chrono::steady_clock::now();
        const auto milliseconds = [](auto start, auto end)
        {
            return std::chrono::duration<double, std::milli>(end - start).count();
        };
        const std::optional<double> gpuMs =
            profiler.hasGpuTimings() ? std::optional(profiler.timings().front().gpuMs) : std::nullopt;
        const FrameTiming timing {m_FrameCount,
                                  milliseconds(frameStart, afterPresent),
                                  milliseconds(frameStart, afterUpdate),
                                  milliseconds(afterUpdate, afterAcquire),
                                  milliseconds(afterAcquire, afterRecord),
                                  milliseconds(afterRecord, afterWait),
                                  milliseconds(afterWait, afterPostRender),
                                  milliseconds(afterPostRender, afterPresent),
                                  gpuMs};
        ++m_FrameCount;
        onFrameComplete(timing);

        return m_Statistics.addFrame(timing.totalMs / 1000.0, timing.updateMs + timing.prepareRecordMs, gpuMs);
    }

    void RenderApp::resetFrames()
    {
        m_FrameCount     = 0;
        m_CloseRequested = false;
        m_PreviousSize   = {};
        m_Statistics     = {};
    }

    void RenderApp::invalidateSurface()
    {
        m_PreviousSize = {};
    }

    void RenderApp::onResize(Extent)
    {
    }

    void RenderApp::onPreRender()
    {
    }

    void RenderApp::onPostRender(Texture&)
    {
    }

    void RenderApp::onRenderSkipped()
    {
    }

    void RenderApp::onPostPresent()
    {
    }

    void RenderApp::onFrameComplete(const FrameTiming&)
    {
    }
} // namespace vultra
