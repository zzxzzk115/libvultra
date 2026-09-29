#pragma once

#include <vultra/core/profiling/profiler.hpp>
#include <vultra/core/rhi/swapchain.hpp>
#include <vultra/function/app/base_app.hpp>

#include <string>

namespace vultra
{
    struct DesktopAppConfig
    {
        std::string title = "Vultra";
        Extent      size {1280, 720};
        bool        validation = true;
        // Matches the old dev AppConfig; tone-mapped output and ImGui use display-encoded values.
        VriFormat swapchainFormat = VriFormat_BGRA8_UNORM;
        // Required VRI features. Device creation fails explicitly when unavailable.
        uint64_t features = 0;
    };

    class DesktopApp : public BaseApp
    {
    public:
        explicit DesktopApp(const DesktopAppConfig& config = {});
        void run(uint64_t frameLimit = 0) final;

        Window& getWindow()
        {
            return m_Window;
        }

        Device& getDevice()
        {
            return m_Device;
        }

        Swapchain& getSwapchain()
        {
            return m_Swapchain;
        }

    protected:
        virtual void onResize(Extent size);
        // CPU preparation, after acquisition/resize and before command recording.
        virtual void onPreRender();
        // Record commands only. Submission and presentation belong to DesktopApp.
        virtual void onRender(VriCommandBuffer* cmd, Texture& target) = 0;
        // GPU has finished; readback and profiler collection are safe here, before present.
        virtual void onPostRender(Texture& target);
        virtual void onPostPresent();
        // Main window cannot render; detached UI windows may still need a frame.
        virtual void onRenderSkipped();

    private:
        Window    m_Window;
        Device    m_Device;
        Swapchain m_Swapchain;
        Frame     m_Frame;
        Profiler  m_FrameProfiler;
    };
} // namespace vultra
