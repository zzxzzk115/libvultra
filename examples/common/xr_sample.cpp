#include "xr_sample.hpp"

#include <algorithm>
#include <thread>

namespace sample
{
    namespace
    {
        VriRect fitEye(vultra::Extent eye, vultra::Extent window, uint32_t index)
        {
            const uint32_t cellWidth = window.width / 2;
            const float    scale     = std::min(float(cellWidth) / eye.width, float(window.height) / eye.height);
            const uint32_t width     = std::max(1u, uint32_t(eye.width * scale));
            const uint32_t height    = std::max(1u, uint32_t(eye.height * scale));
            return {int32_t(index * cellWidth + (cellWidth - width) / 2),
                    int32_t((window.height - height) / 2),
                    width,
                    height};
        }
    } // namespace

    XrSample::XrSample(const Options& options, const std::string& title) :
        m_Options(options),
        m_Window(title.c_str(), {1280, 720}),
        m_System(title.c_str()),
        m_Device(true, m_System.creationHooks()),
        m_Session(m_System, m_Device),
        m_Desktop(m_Device, m_Window, VriFormat_BGRA8_UNORM),
        m_Commands(m_Device),
        m_Mirror(m_Device, m_Desktop.format(), 2),
        m_Gui(m_Device, m_Window, m_Desktop.format()),
        m_Profiler(m_Device),
        m_FrameProfiler(m_Device)
    {
    }

    void XrSample::onImGui()
    {
    }

    void XrSample::onPreRender()
    {
    }

    void XrSample::run(uint64_t frameLimit)
    {
        using namespace vultra;
        XrInstanceProperties runtime {XR_TYPE_INSTANCE_PROPERTIES};
        xrGetInstanceProperties(m_System.instance(), &runtime);
        const bool srgb = eyeFormat() == VriFormat_RGBA8_SRGB || eyeFormat() == VriFormat_BGRA8_SRGB;
        Logger::app().info("OpenXR: {}; eye format {} ({})",
                           runtime.runtimeName,
                           int(eyeFormat()),
                           srgb ? "sRGB" : "linear UNORM");
        uint64_t eyeFrames       = 0;
        m_FrameCount             = 0;
        m_CloseRequested         = false;
        auto            previous = std::chrono::steady_clock::now();
        FrameStatistics statistics;
        while (!m_CloseRequested && !m_Session.exiting() && (!frameLimit || m_FrameCount < frameLimit))
        {
            const auto frameStart = std::chrono::steady_clock::now();
            if (!m_Window.poll())
            {
                break;
            }
            if (m_Window.input().isKeyHeld(KeyCode::eEscape))
            {
                break;
            }
            const auto  now     = std::chrono::steady_clock::now();
            const float seconds = std::chrono::duration<float>(now - previous).count();
            previous            = now;
            onPreUpdate(seconds);
            onUpdate(seconds);
            onPostUpdate(seconds);
            if (m_CloseRequested)
            {
                break;
            }
            double cpuMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
            const auto frame         = m_Session.beginFrame();
            auto*      desktopTarget = m_Desktop.acquire();
            const auto renderStart   = std::chrono::steady_clock::now();
            m_Gui.begin();
            ImGui::SetNextWindowSize({370, 200}, ImGuiCond_FirstUseEver);
            ImGui::Begin("OpenXR Mirror");
            ImGui::Text("Runtime: %s", runtime.runtimeName);
            ImGui::Text("Eye format: %s", srgb ? "sRGB (hardware encode)" : "UNORM (linear)");
            ImGui::TextUnformatted("Left eye | Right eye; aspect ratio preserved");
            ImGui::Text("Status: %s", frame.shouldRender ? "Rendering" : "Waiting for active views");
            ImGui::Text("Submitted eye frames: %llu", static_cast<unsigned long long>(eyeFrames));
            for (const auto& timing : m_Profiler.timings())
            {
                ImGui::Text("%s: %.3f ms", timing.name.c_str(), timing.gpuMs);
            }
            ImGui::End();
            onImGui();
            m_Gui.upload(m_Desktop.size());
            onPreRender();
            auto* cmd = m_Commands.begin();
            m_FrameProfiler.beginFrame(cmd);
            m_FrameProfiler.beginPass(cmd, "XR frame and mirror");
            m_Gui.copy(cmd);
            m_Profiler.beginFrame(cmd);
            if (frame.shouldRender)
            {
                for (uint32_t eye = 0; eye < 2; ++eye)
                {
                    m_Profiler.beginPass(cmd, eye == 0 ? "Left eye" : "Right eye");
                    onRenderEye(cmd, frame.eyes[eye], eye);
                    m_Profiler.endPass(cmd);
                    m_Mirror.setSource(eye, *frame.eyes[eye].color);
                }
                ++eyeFrames;
            }
            if (desktopTarget)
            {
                desktopTarget->transition(cmd,
                                          {VriAccess_ColorAttachmentWrite,
                                           VriLayout_ColorAttachment,
                                           VriPipelineStage_ColorAttachmentOutput});
                const float black[4] {0, 0, 0, 1};
                beginColorPass(m_Device, cmd, desktopTarget->view(), m_Desktop.size(), black);
                m_Device.core.CmdEndRendering(cmd);
                if (frame.shouldRender && m_Desktop.size().width >= 2)
                {
                    for (uint32_t eye = 0; eye < 2; ++eye)
                    {
                        const auto& texture = *frame.eyes[eye].color;
                        // XR sources are linear when sampled, including hardware-decoded sRGB images.
                        m_Mirror.draw(cmd,
                                      *desktopTarget,
                                      fitEye({texture.desc.width, texture.desc.height}, m_Desktop.size(), eye),
                                      eye,
                                      true);
                    }
                }
                desktopTarget->transition(cmd,
                                          {VriAccess_ColorAttachmentRead | VriAccess_ColorAttachmentWrite,
                                           VriLayout_ColorAttachment,
                                           VriPipelineStage_ColorAttachmentOutput});
                beginColorPass(m_Device, cmd, desktopTarget->view(), m_Desktop.size());
                m_Gui.draw(cmd);
                m_Device.core.CmdEndRendering(cmd);
                desktopTarget->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
            }
            m_Profiler.resolve(cmd);
            // Complete mirror reads before releasing acquired XR images to the runtime.
            m_Session.prepareSubmit(cmd);
            m_FrameProfiler.endPass(cmd);
            m_FrameProfiler.resolve(cmd);
            cpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderStart).count();
            m_Commands.submitAndWait();
            m_Profiler.collect();
            m_FrameProfiler.collect();
            m_Session.endFrame();
            if (desktopTarget)
            {
                captureFrame(m_Options, m_FrameCount, m_Device, *desktopTarget);
                m_Desktop.present();
            }
            m_Gui.renderPlatformWindows();
            ++m_FrameCount;
            if (!frame.begun)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const double frameSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count();
            const std::optional<double> gpuMs =
                m_FrameProfiler.hasGpuTimings() ? std::optional(m_FrameProfiler.timings().front().gpuMs) : std::nullopt;
            if (auto suffix = statistics.addFrame(frameSeconds, cpuMs, gpuMs))
            {
                m_Window.setTitleSuffix(std::move(*suffix));
            }
        }
        Logger::app().info("Completed {} application frames, {} XR eye frames", m_FrameCount, eyeFrames);
    }
} // namespace sample
