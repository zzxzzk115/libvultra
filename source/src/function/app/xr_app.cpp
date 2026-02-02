#include "vultra/function/app/xr_app.hpp"
#include "vultra/function/openxr/xr_device.hpp"
#include "vultra/function/service/services.hpp"

namespace vultra
{
    XRApp::XRApp(std::span<char*> args, const AppConfig& appConfig, const XRConfig& xrConfig) :
        ImGuiApp(args, appConfig, xrConfig.imguiConfig, xrConfig.clearColor), m_Headset(*m_RenderDevice),
        m_CommonAction(m_RenderDevice->getXRDevice()->getXrInstance(),
                       m_Headset.getXrSession(),
                       m_RenderDevice->getXRDevice()->getProperties().supportEyeTracking)
    {}

    void XRApp::run()
    {
        FPSMonitor fpsMonitor {m_Window};

        const fsec targetFrameTime {1.0 / 60.0f};
        fsec       deltaTime {targetFrameTime};
        fsec       accumulator {0};

        // https://github.com/janhsimon/openxr-vulkan-example/blob/main/src/Main.cpp

        while (!m_Window.shouldClose() && !m_Headset.isExitRequested())
        {
            using clock           = std::chrono::high_resolution_clock;
            const auto beginTicks = clock::now();

            {
                ZoneScopedN("[App] PreUpdate");
                onPreUpdate(deltaTime);
            }
            {
                ZoneScopedN("[App] PollEvents");
                m_Window.pollEvents();
            }
            if (!m_IsRunning)
                break;

            uint32_t                                  swapchainImageIndex;
            const openxr::XRHeadset::BeginFrameResult frameResult = m_Headset.beginFrame(swapchainImageIndex);
            if (frameResult == openxr::XRHeadset::BeginFrameResult::eError)
            {
                break;
            }
            else if (frameResult == openxr::XRHeadset::BeginFrameResult::eSkipAll)
            {
                VULTRA_CORE_TRACE("[App] Headset skipped one frame, continuing to the next iteration");
                continue;
            }

            // Sync controllers & eye tracker
            if (!m_CommonAction.sync(m_Headset.getXrSpace(), m_Headset.getXrFrameState().predictedDisplayTime))
            {
                break;
            }

            {
                ZoneScopedN("[App] Update");
                onUpdate(deltaTime);
            }
            {
                ZoneScopedN("[App] PhysicsUpdate");
                accumulator += (deltaTime < targetFrameTime ? deltaTime : targetFrameTime);
                while (accumulator >= targetFrameTime)
                {
                    onPhysicsUpdate(targetFrameTime);
                    accumulator -= targetFrameTime;
                }
            }
            {
                ZoneScopedN("[App] PostUpdate");
                onPostUpdate(deltaTime);
            }

            renderDocCaptureBegin();

            bool acquiredNextFrame = m_FrameController.acquireNextFrame();

            // Begin frame
            auto& cb = m_FrameController.beginFrame();

            // XR Rendering
            {
                auto& xrRenderTargetView = m_Headset.getSwapchainStereoRenderTargetView(swapchainImageIndex);
                {
                    ZoneScopedN("XRApp::onXrRender");
                    onXrRender(cb, xrRenderTargetView, deltaTime);
                }
            }

            if (m_Swapchain)
            {
                {
                    ZoneScopedN("[App] PreRender");
                    onPreRender();
                }

                // Normal Rendering (ImGui, Mirror View, etc.)
                {
                    ZoneScopedN("[App] Render");

                    // Only render if we successfully acquired the next frame
                    if (acquiredNextFrame)
                    {
                        onRender(cb, m_FrameController.getCurrentTarget(), deltaTime);

                        if (dd::hasPendingDraws())
                        {
                            auto&                      backBuffer = m_FrameController.getCurrentTarget().texture;
                            const rhi::FramebufferInfo framebufferInfo {
                                .area             = rhi::Rect2D {.extent = backBuffer.getExtent()},
                                .colorAttachments = {
                                    {
                                        .target = &backBuffer,
                                    },
                                }};
                            commonContext.debugDraw->updateColorFormat(backBuffer.getPixelFormat());
                            commonContext.debugDraw->bindDepthTexture(nullptr); // No depth texture bound
                            commonContext.debugDraw->buildPipelineIfNeeded();
                            commonContext.debugDraw->beginFrame(cb, framebufferInfo);
                            dd::flush(deltaTime.count());
                            commonContext.debugDraw->endFrame();
                        }
                    }
                }
            }

            m_FrameController.endFrame();

            renderDocCaptureEnd();

            {
                ZoneScopedN("[App] PostRender");
                onPostRender();
            }

            // Only present if we successfully acquired the next frame
            if (acquiredNextFrame)
            {
                ZoneScopedN("[App] Present");
                m_FrameController.present();
                m_FrameCounter++;
            }

            FrameMark;

            deltaTime = clock::now() - beginTicks;
            if (deltaTime > 1s)
                deltaTime = targetFrameTime;

            fpsMonitor.update(deltaTime);

            m_Headset.endFrame();
        }

        dd::shutdown();
        commonContext.cleanup();
        m_RenderDevice->waitIdle();
        service::Services::reset();
    }

    void XRApp::onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt)
    {
        {
            ZoneScopedN("XRApp::onNormalRender");
            onNormalRender(cb, rtv, dt);
        }

        ImGuiApp::onRender(cb, rtv, dt);
    }
} // namespace vultra