#pragma once

#include <memory>

namespace vultra
{
    struct DesktopAppConfig;
    class Window;
    class Device;
    class Swapchain;
    class Frame;
    class Profiler;
    class RenderingServer;

    // Owns desktop services in construction order. The GPU is idle after each submitted frame.
    class RuntimeContext
    {
    public:
        explicit RuntimeContext(const DesktopAppConfig& config);
        ~RuntimeContext();
        RuntimeContext(const RuntimeContext&)            = delete;
        RuntimeContext& operator=(const RuntimeContext&) = delete;

        Window&          window();
        Device&          device();
        Swapchain&       swapchain();
        Frame&           frame();
        Profiler&        profiler();
        RenderingServer& rendering();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
