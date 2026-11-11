#include <vultra/app/render_app.hpp>

#include <chrono>

namespace vultra
{
    namespace
    {
        std::expected<bool, Error> copyToSwapchain(Device& device, Frame& frame, Swapchain& swapchain, Texture& source)
        {
            // Browser canvas images expire on an event-loop yield. Acquire only after rendering/readback
            // has finished, then record and submit the copy before the next GPU wait can yield.
            const auto canvas = swapchain.acquire();
            if (!canvas)
            {
                return std::unexpected(canvas.error());
            }
            if (!*canvas)
            {
                return false;
            }
            const auto cmd = frame.begin();
            if (!cmd)
            {
                return std::unexpected(cmd.error());
            }
            source.transition(*cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*canvas)->transition(
                *cmd,
                {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            VriTextureCopyDesc copy {};
            copy.src.layerNum = 1;
            copy.dst.layerNum = 1;
            copy.src.aspect   = VriImageAspect_Color;
            copy.dst.aspect   = VriImageAspect_Color;
            device.core.CmdCopyTexture(*cmd, (*canvas)->handle, source.handle, &copy);
            (*canvas)->transition(*cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
            if (auto submitted = frame.submitAndWait(); !submitted)
            {
                return std::unexpected(submitted.error());
            }
            return true;
        }
    } // namespace

    RenderApp::RenderApp(const AppConfig& config) :
        m_RenderDocFrame(config.renderDocFrame)
    {
    }

    std::expected<std::optional<std::string>, Error> RenderApp::advanceFrame(Device&    device,
                                                                             Swapchain& swapchain,
                                                                             Frame&     frame,
                                                                             Profiler&  profiler,
                                                                             float      deltaSeconds,
                                                                             bool       renderable,
                                                                             Texture*   presentSource)
    {
        const auto frameStart = std::chrono::steady_clock::now();
        if (auto updated = onPreUpdate(deltaSeconds); !updated)
        {
            return std::unexpected(updated.error());
        }
        if (auto updated = onUpdate(deltaSeconds); !updated)
        {
            return std::unexpected(updated.error());
        }
        if (auto updated = onPostUpdate(deltaSeconds); !updated)
        {
            return std::unexpected(updated.error());
        }
        if (m_CloseRequested)
        {
            return std::nullopt;
        }
        if (!renderable)
        {
            if (auto skipped = onRenderSkipped(); !skipped)
            {
                return std::unexpected(skipped.error());
            }
            return std::nullopt;
        }
        const auto afterUpdate = std::chrono::steady_clock::now();

        Texture* target = presentSource;
        if (!target)
        {
            const auto acquired = swapchain.acquire();
            if (!acquired)
            {
                return std::unexpected(acquired.error());
            }
            target = *acquired;
        }
        if (!target)
        {
            if (auto skipped = onRenderSkipped(); !skipped)
            {
                return std::unexpected(skipped.error());
            }
            return std::nullopt;
        }
        const auto afterAcquire = std::chrono::steady_clock::now();
#ifdef VULTRA_WITH_RENDERDOC
        if (m_RenderDocFrame == m_FrameCount)
        {
            if (auto begun = m_RenderDoc->begin(); !begun)
            {
                return std::unexpected(begun.error());
            }
        }
#endif
        if (m_PreviousSize != swapchain.size())
        {
            m_PreviousSize = swapchain.size();
            if (auto resized = onResize(m_PreviousSize); !resized)
            {
                return std::unexpected(resized.error());
            }
        }
        // Once acquired, finish and present this frame even if a render callback calls close().
        if (auto prepared = onPreRender(); !prepared)
        {
            return std::unexpected(prepared.error());
        }
        const auto begun = frame.begin();
        if (!begun)
        {
            return std::unexpected(begun.error());
        }
        auto* cmd = *begun;
        device.core.CmdBeginDebugGroup(cmd, "Frame");
        profiler.beginFrame(cmd);
        if (auto profiled = profiler.beginPass(cmd, "Frame"); !profiled)
        {
            return std::unexpected(profiled.error());
        }
        if (auto rendered = onRender(cmd, *target); !rendered)
        {
            return std::unexpected(rendered.error());
        }
        if (!presentSource)
        {
            target->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
        }
        if (auto profiled = profiler.endPass(cmd); !profiled)
        {
            return std::unexpected(profiled.error());
        }
        if (auto profiled = profiler.resolve(cmd); !profiled)
        {
            return std::unexpected(profiled.error());
        }
        device.core.CmdEndDebugGroup(cmd);
        const auto afterRecord = std::chrono::steady_clock::now();
        if (auto submitted = frame.submitAndWait(); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        const auto afterWait = std::chrono::steady_clock::now();
        if (auto collected = profiler.collect(); !collected)
        {
            return std::unexpected(collected.error());
        }
        if (auto completed = onPostRender(*target); !completed)
        {
            return std::unexpected(completed.error());
        }
        const auto afterPostRender = std::chrono::steady_clock::now();
        if (presentSource)
        {
            const auto copied = copyToSwapchain(device, frame, swapchain, *presentSource);
            if (!copied)
            {
                return std::unexpected(copied.error());
            }
            if (!*copied)
            {
                if (auto skipped = onRenderSkipped(); !skipped)
                {
                    return std::unexpected(skipped.error());
                }
                return std::nullopt;
            }
        }
        if (auto presented = swapchain.present(); !presented)
        {
            return std::unexpected(presented.error());
        }
        if (auto presented = onPostPresent(); !presented)
        {
            return std::unexpected(presented.error());
        }
#ifdef VULTRA_WITH_RENDERDOC
        if (m_RenderDocFrame == m_FrameCount)
        {
            if (auto ended = m_RenderDoc->end(); !ended)
            {
                return std::unexpected(ended.error());
            }
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
        if (auto completed = onFrameComplete(timing); !completed)
        {
            return std::unexpected(completed.error());
        }

        return m_Statistics.addFrame(timing.totalMs / 1000.0, timing.updateMs + timing.prepareRecordMs, gpuMs);
    }

    std::expected<void, Error> RenderApp::resetFrames()
    {
        m_FrameCount     = 0;
        m_CloseRequested = false;
        m_PreviousSize   = {};
        m_Statistics     = {};
#ifdef VULTRA_WITH_RENDERDOC
        if (m_RenderDocFrame && !m_RenderDoc)
        {
            auto capture = RenderDocCapture::create();
            if (!capture)
            {
                return std::unexpected(capture.error());
            }
            m_RenderDoc = std::move(*capture);
        }
#else
        if (m_RenderDocFrame)
        {
            return makeError("This platform build does not provide RenderDoc capture");
        }
#endif
        return {};
    }

    void RenderApp::invalidateSurface()
    {
        m_PreviousSize = {};
    }

    std::expected<void, Error> RenderApp::onResize(Extent)
    {
        return {};
    }

    std::expected<void, Error> RenderApp::onPreRender()
    {
        return {};
    }

    std::expected<void, Error> RenderApp::onPostRender(Texture&)
    {
        return {};
    }

    std::expected<void, Error> RenderApp::onRenderSkipped()
    {
        return {};
    }

    std::expected<void, Error> RenderApp::onPostPresent()
    {
        return {};
    }

    std::expected<void, Error> RenderApp::onFrameComplete(const FrameTiming&)
    {
        return {};
    }
} // namespace vultra
