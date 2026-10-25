#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/drivers/rhi/swapchain.hpp>
#include <vultra/main/app/app_config.hpp>
#include <vultra/main/runtime_context.hpp>
#include <vultra/platform/os/window.hpp>

namespace vultra
{
    struct RuntimeContext::Impl
    {
        explicit Impl(const AppConfig& config) :
            window(config.title.c_str(), config.size),
            device(config.validation, nullptr, config.features),
            swapchain(device, window, config.swapchainFormat),
            frame(device),
            profiler(device)
        {
        }

        // Applications own optional renderers/catalogs; their members die before this desktop context.
        Window    window;
        Device    device;
        Swapchain swapchain;
        Frame     frame;
        Profiler  profiler;
    };

    RuntimeContext::RuntimeContext(const AppConfig& config) :
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
} // namespace vultra
