#include "support/swapchain_expected_probe.hpp"

#include <vultra/platform/os/window.hpp>

#include <SDL3/SDL.h>

#include <iostream>
#include <limits>

int main()
{
    if (vultra::Window::create(nullptr, {128, 96}) || vultra::Window::create("Invalid", {0, 96}) ||
        vultra::Window::create("Invalid", {std::numeric_limits<uint32_t>::max(), 96}) ||
        vultra::Window::borrow(nullptr))
    {
        std::cerr << "Window creation accepted invalid input\n";
        return 1;
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return 1;
    auto* native =
        SDL_CreateWindow("Swapchain test",
                         128,
                         96,
                         SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!native)
    {
        std::cerr << SDL_GetError() << '\n';
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return 1;
    }
    auto result = [&]() -> std::expected<void, std::string>
    {
        auto window = vultra::Window::borrow(native);
        if (!window)
            return std::unexpected(window.error());
        auto framebuffer = (*window)->framebufferSizeExpected();
        auto logical     = (*window)->sizeExpected();
        if (!framebuffer || framebuffer->empty() || !logical || logical->empty())
            return std::unexpected("Borrowed window size query failed");
        auto device = vultra::Device::create(true);
        if (!device)
            return std::unexpected(device.error());
        auto swapchain = vultra::Swapchain::create(**device, **window, VriFormat_RGBA8_UNORM);
        if (!swapchain)
            return std::unexpected(swapchain.error());
        return vultra::testing::swapchainExpectedProbe(
            **device,
            **swapchain,
            [&](vultra::Swapchain& swap) -> std::expected<void, std::string>
            {
                if (!SDL_SetWindowSize(native, 160, 120))
                    return std::unexpected(std::string("Resize SDL window: ") + SDL_GetError());
                SDL_PumpEvents();
                auto resized = (*window)->framebufferSizeExpected();
                if (!resized || resized->empty())
                    return std::unexpected("Resized window has no framebuffer");
                if (auto updated = swap.resizeExpected(*resized); !updated)
                    return updated;
                if (auto updated = swap.setVsyncExpected(false); !updated)
                    return updated;
                if (auto updated = swap.setVsyncExpected(true); !updated)
                    return updated;
                return {};
            });
    }();
    // The wrapper borrows the SDL window; it must remain usable after the wrapper is destroyed.
    int        width                  = 0;
    int        height                 = 0;
    const bool borrowedWindowSurvived = SDL_GetWindowSize(native, &width, &height) && width > 0 && height > 0;
    SDL_DestroyWindow(native);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if (!result || !borrowedWindowSurvived)
    {
        std::cerr << (result ? "Borrowed window was destroyed" : result.error()) << '\n';
        return 1;
    }
    std::cout << "SDL window and swapchain expected surface presentation passed\n";
    return 0;
}
