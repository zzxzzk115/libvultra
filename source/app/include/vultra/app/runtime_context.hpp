#pragma once

#include <vultra/core/base/error.hpp>

#include <expected>
#include <memory>

namespace vultra
{
    struct AppConfig;
    class Window;
    class Device;
    class Swapchain;
    class Frame;
    class Profiler;

    // Owns desktop services in construction order. The GPU is idle after each submitted frame. Moving keeps
    // every service at its address.
    class RuntimeContext
    {
    public:
        [[nodiscard]] static std::expected<RuntimeContext, Error> create(const AppConfig& config);
        ~RuntimeContext();
        RuntimeContext(RuntimeContext&&) noexcept;
        RuntimeContext& operator=(RuntimeContext&&) noexcept;
        RuntimeContext(const RuntimeContext&)            = delete;
        RuntimeContext& operator=(const RuntimeContext&) = delete;

        Window&    window();
        Device&    device();
        Swapchain& swapchain();
        Frame&     frame();
        Profiler&  profiler();

    private:
        struct Impl;
        explicit RuntimeContext(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
