#include <vultra/function/app/desktop_app.hpp>

#include <GLFW/glfw3.h>

#include <chrono>

namespace vultra
{
    DesktopApp::DesktopApp(const DesktopAppConfig& config) :
        m_Window(config.title.c_str(), config.size),
        m_Device(config.validation, nullptr, config.features),
        m_Swapchain(m_Device, m_Window, config.swapchainFormat),
        m_Frame(m_Device),
        m_FrameProfiler(m_Device)
    {
    }

    void DesktopApp::run(uint64_t frameLimit)
    {
        auto            previous = std::chrono::steady_clock::now();
        Extent          previousSize {};
        FrameStatistics statistics;
        m_FrameCount     = 0;
        m_CloseRequested = false;
        while (!m_CloseRequested && (!frameLimit || m_FrameCount < frameLimit))
        {
            const auto frameStart = std::chrono::steady_clock::now();
            if (!m_Window.poll())
            {
                break;
            }
            if (m_Window.input().isKeyHeld(KeyCode::eEscape))
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
            if (glfwGetWindowAttrib(m_Window.handle(), GLFW_ICONIFIED))
            {
                onRenderSkipped();
                glfwWaitEventsTimeout(0.01);
                continue;
            }
            double cpuMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
            auto* target = m_Swapchain.acquire();
            if (!target)
            {
                onRenderSkipped();
                glfwWaitEventsTimeout(0.01);
                continue;
            }
            const auto renderStart = std::chrono::steady_clock::now();
            if (previousSize != m_Swapchain.size())
            {
                previousSize = m_Swapchain.size();
                onResize(previousSize);
            }
            // Once acquired, finish and present this frame even if a render callback calls close().
            onPreRender();
            auto* cmd = m_Frame.begin();
            m_FrameProfiler.beginFrame(cmd);
            m_FrameProfiler.beginPass(cmd, "Frame");
            onRender(cmd, *target);
            target->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
            m_FrameProfiler.endPass(cmd);
            m_FrameProfiler.resolve(cmd);
            cpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderStart).count();
            m_Frame.submitAndWait();
            m_FrameProfiler.collect();
            onPostRender(*target);
            m_Swapchain.present();
            onPostPresent();
            ++m_FrameCount;
            const double frameSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count();
            const std::optional<double> gpuMs =
                m_FrameProfiler.hasGpuTimings() ? std::optional(m_FrameProfiler.timings().front().gpuMs) : std::nullopt;
            if (auto suffix = statistics.addFrame(frameSeconds, cpuMs, gpuMs))
            {
                m_Window.setTitleSuffix(std::move(*suffix));
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
} // namespace vultra
