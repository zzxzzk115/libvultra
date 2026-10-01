#include <vultra/main/app/desktop_app.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>

#include <chrono>

namespace vultra
{
    DesktopApp::DesktopApp(const DesktopAppConfig& config) :
        m_Context(std::make_unique<RuntimeContext>(config)),
        m_RenderDocFrame(config.renderDocFrame),
        m_RenderDoc(config.renderDocFrame ? std::make_unique<RenderDocCapture>() : nullptr)
    {
    }

    DesktopApp::~DesktopApp() = default;

    void DesktopApp::run(uint64_t frameLimit)
    {
        auto&           window    = m_Context->window();
        auto&           device    = m_Context->device();
        auto&           swapchain = m_Context->swapchain();
        auto&           frame     = m_Context->frame();
        auto&           profiler  = m_Context->profiler();
        auto            previous  = std::chrono::steady_clock::now();
        Extent          previousSize {};
        FrameStatistics statistics;
        m_FrameCount     = 0;
        m_CloseRequested = false;
        while (!m_CloseRequested && (!frameLimit || m_FrameCount < frameLimit))
        {
            const auto frameStart = std::chrono::steady_clock::now();
            if (!window.poll())
            {
                break;
            }
            if (window.input().isKeyHeld(KeyCode::eEscape))
            {
                close();
                break;
            }
            const auto  now          = std::chrono::steady_clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - previous).count();
            previous                 = now;
            onPreUpdate(deltaSeconds);
            onUpdate(deltaSeconds);
            onPostUpdate(deltaSeconds);
            if (m_CloseRequested)
            {
                break;
            }
            if (window.minimized())
            {
                onRenderSkipped();
                Window::waitEvents(0.01);
                continue;
            }
            const auto afterUpdate = std::chrono::steady_clock::now();
            auto*      target      = swapchain.acquire();
            if (!target)
            {
                onRenderSkipped();
                Window::waitEvents(0.01);
                continue;
            }
            const auto afterAcquire = std::chrono::steady_clock::now();
            if (m_RenderDocFrame == m_FrameCount)
            {
                m_RenderDoc->begin();
            }
            if (previousSize != swapchain.size())
            {
                previousSize = swapchain.size();
                onResize(previousSize);
            }
            // Once acquired, finish and present this frame even if a render callback calls close().
            onPreRender();
            auto* cmd = frame.begin();
            device.core.CmdBeginDebugGroup(cmd, "Frame");
            profiler.beginFrame(cmd);
            profiler.beginPass(cmd, "Frame");
            onRender(cmd, *target);
            target->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
            profiler.endPass(cmd);
            profiler.resolve(cmd);
            device.core.CmdEndDebugGroup(cmd);
            const auto afterRecord = std::chrono::steady_clock::now();
            frame.submitAndWait();
            m_Context->rendering().collectCompletedFrame();
            const auto afterWait = std::chrono::steady_clock::now();
            profiler.collect();
            onPostRender(*target);
            const auto afterPostRender = std::chrono::steady_clock::now();
            swapchain.present();
            onPostPresent();
            if (m_RenderDocFrame == m_FrameCount)
            {
                m_RenderDoc->end();
            }
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
            if (auto suffix =
                    statistics.addFrame(timing.totalMs / 1000.0, timing.updateMs + timing.prepareRecordMs, gpuMs))
            {
                window.setTitleSuffix(std::move(*suffix));
            }
        }
    }

    void DesktopApp::onResize(Extent)
    {
    }

    void DesktopApp::onPreRender()
    {
    }

    void DesktopApp::onPostRender(Texture&)
    {
    }

    void DesktopApp::onRenderSkipped()
    {
    }

    void DesktopApp::onPostPresent()
    {
    }

    void DesktopApp::onFrameComplete(const FrameTiming&)
    {
    }
} // namespace vultra
