#pragma once

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/logger.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/core/profiling/renderdoc_api.hpp"
#include "vultra/core/rhi/frame_controller.hpp"
#include "vultra/core/rhi/frame_index.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <cpptrace/from_current.hpp>

namespace vultra
{
    struct AppConfig
    {
        std::string                      title {"Untitled Vultra Application"};
        uint32_t                         width {1024};
        uint32_t                         height {768};
        rhi::FrameIndex::ValueType       numFramesInFlight {2};
        rhi::RenderDeviceFeatureFlagBits renderDeviceFeatureFlag {rhi::RenderDeviceFeatureFlagBits::eNormal};
        Logger::Level                    logLevel {Logger::Level::eTrace};
        rhi::VerticalSync                vSyncConfig {rhi::VerticalSync::eAdaptive};
        rhi::Swapchain::Format           swapchainFormat {rhi::Swapchain::Format::eLinear};
    };

    class BaseApp
    {
    public:
        BaseApp(std::span<char*> args, const AppConfig& cfg);
        BaseApp(const BaseApp&)     = delete;
        BaseApp(BaseApp&&) noexcept = delete;
        virtual ~BaseApp()          = default;

        BaseApp& operator=(const BaseApp&)     = delete;
        BaseApp& operator=(BaseApp&&) noexcept = delete;

        [[nodiscard]] os::Window&        getWindow();
        [[nodiscard]] rhi::RenderDevice& getRenderDevice();
        [[nodiscard]] rhi::Swapchain&    getSwapchain();

        virtual void run();
        void         close();

    protected:
        void setupWindowCallbacks();

        virtual void onGeneralWindowEvent(const os::GeneralWindowEvent& event);
        virtual void onResize(uint32_t width, uint32_t height);

        virtual void onPreUpdate(const fsec);
        virtual void onUpdate(const fsec) {}
        virtual void onPhysicsUpdate(const fsec) {}
        virtual void onPostUpdate(const fsec) {}

        virtual void onPreRender() {}
        virtual void onRender(rhi::CommandBuffer&, const rhi::RenderTargetView, const fsec) {}
        virtual void onPostRender() {}

        void renderDocCaptureBegin();
        void renderDocCaptureEnd();

    protected:
        bool     m_IsRunning {true};
        bool     m_Minimized {false};
        bool     m_WantCaptureFrame {false};
        uint64_t m_FrameCounter {0};

        Scope<RenderDocAPI>      m_RenderDocAPI {nullptr};
        os::Window               m_Window;
        Scope<rhi::RenderDevice> m_RenderDevice {nullptr};
        rhi::Swapchain           m_Swapchain;
        rhi::FrameController     m_FrameController;
    };

    class FPSMonitor final
    {
    public:
        explicit FPSMonitor(os::Window& window) : m_Target {window}, m_OriginalTile {window.getTitle()} {}

        void update(const fsec dt)
        {
            ++m_NumFrames;
            m_Time += dt;

            if (m_Time >= 1s)
            {
                m_Target.setTitle(std::format("{} | FPS = {}", m_OriginalTile, m_NumFrames));

                m_Time      = 0s;
                m_NumFrames = 0;
            }
        }

    private:
        os::Window&       m_Target;
        const std::string m_OriginalTile;

        uint32_t m_NumFrames {0};
        fsec     m_Time {0s};
    };
} // namespace vultra

#define CONFIG_MAIN(AppClass) \
    int main(int argc, char* argv[]) \
    { \
        CPPTRACE_TRY \
        { \
            AppClass app {std::span {argv, std::size_t(argc)}}; \
            app.run(); \
        } \
        CPPTRACE_CATCH(const std::exception& e) \
        { \
            VULTRA_CLIENT_CRITICAL(e.what()); \
            cpptrace::from_current_exception().print(); \
            return -1; \
        } \
        return 0; \
    }
