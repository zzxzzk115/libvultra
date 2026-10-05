#pragma once

#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/drivers/profiling/renderdoc_capture.hpp>
#include <vultra/drivers/rhi/swapchain.hpp>
#include <vultra/main/app/base_app.hpp>
#include <vultra/main/runtime_context.hpp>

#include <memory>
#include <optional>
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
        uint64_t                features = 0;
        std::optional<uint64_t> renderDocFrame; // zero-based; requires an injected RenderDoc session
    };

    class DesktopApp : public BaseApp
    {
    public:
        explicit DesktopApp(const DesktopAppConfig& config = {});
        ~DesktopApp() override;
        void run(uint64_t frameLimit = 0) final;

        Window& getWindow()
        {
            return m_Context->window();
        }

        Device& getDevice()
        {
            return m_Context->device();
        }

        RenderingServer& getRenderingServer()
        {
            return m_Context->rendering();
        }

        PassCatalog& getPassCatalog()
        {
            return m_Context->passes();
        }

        Swapchain& getSwapchain()
        {
            return m_Context->swapchain();
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
        virtual void onFrameComplete(const FrameTiming& timing);
        // Main window cannot render; detached UI windows may still need a frame.
        virtual void onRenderSkipped();

    private:
        std::unique_ptr<RuntimeContext>   m_Context;
        std::optional<uint64_t>           m_RenderDocFrame;
        std::unique_ptr<RenderDocCapture> m_RenderDoc;
    };
} // namespace vultra
