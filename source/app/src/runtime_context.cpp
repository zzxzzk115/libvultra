#include <vultra/app/app_config.hpp>
#include <vultra/app/runtime_context.hpp>
#include <vultra/gpu/profiling/profiler.hpp>
#include <vultra/gpu/swapchain.hpp>
#include <vultra/platform/os/window.hpp>

namespace vultra
{
    struct RuntimeContext::Impl
    {
        // Applications own optional renderers/catalogs; their members die before this desktop context.
        // Members are destroyed in reverse order: profiler, frame, swapchain, device, window.
        std::unique_ptr<Window>    window;
        std::unique_ptr<Device>    device;
        std::unique_ptr<Swapchain> swapchain;
        std::unique_ptr<Frame>     frame;
        std::unique_ptr<Profiler>  profiler;
    };

    RuntimeContext::RuntimeContext(std::unique_ptr<Impl> impl) :
        m_Impl(std::move(impl))
    {
    }

    std::expected<RuntimeContext, Error> RuntimeContext::create(const AppConfig& config)
    {
        auto impl   = std::make_unique<Impl>();
        auto window = Window::create(config.title.c_str(), config.size);
        if (!window)
        {
            return std::unexpected(window.error());
        }
        impl->window = std::move(*window);
        auto device  = Device::create(config.validation, nullptr, config.features);
        if (!device)
        {
            return std::unexpected(device.error());
        }
        impl->device   = std::move(*device);
        auto swapchain = Swapchain::create(*impl->device, *impl->window, config.swapchainFormat);
        if (!swapchain)
        {
            return std::unexpected(swapchain.error());
        }
        impl->swapchain = std::move(*swapchain);
        auto frame      = Frame::create(*impl->device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        impl->frame   = std::move(*frame);
        auto profiler = Profiler::create(*impl->device);
        if (!profiler)
        {
            return std::unexpected(profiler.error());
        }
        impl->profiler = std::move(*profiler);
        return RuntimeContext(std::move(impl));
    }

    RuntimeContext::RuntimeContext(RuntimeContext&&) noexcept            = default;
    RuntimeContext& RuntimeContext::operator=(RuntimeContext&&) noexcept = default;
    RuntimeContext::~RuntimeContext()                                    = default;

    Window& RuntimeContext::window()
    {
        return *m_Impl->window;
    }

    Device& RuntimeContext::device()
    {
        return *m_Impl->device;
    }

    Swapchain& RuntimeContext::swapchain()
    {
        return *m_Impl->swapchain;
    }

    Frame& RuntimeContext::frame()
    {
        return *m_Impl->frame;
    }

    Profiler& RuntimeContext::profiler()
    {
        return *m_Impl->profiler;
    }
} // namespace vultra
