#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/drivers/rhi/swapchain.hpp>
#include <vultra/main/app/desktop_app.hpp>
#include <vultra/main/runtime_context.hpp>
#include <vultra/servers/rendering/graph/pass_catalog.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>

namespace vultra
{
    struct RuntimeContext::Impl
    {
        explicit Impl(const DesktopAppConfig& config) :
            window(config.title.c_str(), config.size),
            device(config.validation, nullptr, config.features),
            swapchain(device, window, config.swapchainFormat),
            frame(device),
            profiler(device),
            rendering(device),
            passes(device)
        {
        }

        // Reverse destruction releases server resources and frame objects before the device and window.
        Window          window;
        Device          device;
        Swapchain       swapchain;
        Frame           frame;
        Profiler        profiler;
        RenderingServer rendering;
        PassCatalog     passes;
    };

    RuntimeContext::RuntimeContext(const DesktopAppConfig& config) :
        m_Impl(std::make_unique<Impl>(config))
    {
    }

    RuntimeContext::~RuntimeContext() = default;

    Window& RuntimeContext::window()
    {
        return m_Impl->window;
    }

    Device& RuntimeContext::device()
    {
        return m_Impl->device;
    }

    Swapchain& RuntimeContext::swapchain()
    {
        return m_Impl->swapchain;
    }

    Frame& RuntimeContext::frame()
    {
        return m_Impl->frame;
    }

    Profiler& RuntimeContext::profiler()
    {
        return m_Impl->profiler;
    }

    RenderingServer& RuntimeContext::rendering()
    {
        return m_Impl->rendering;
    }

    PassCatalog& RuntimeContext::passes()
    {
        return m_Impl->passes;
    }
} // namespace vultra
