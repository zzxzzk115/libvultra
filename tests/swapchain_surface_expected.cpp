#include "support/swapchain_expected_probe.hpp"

#include <vultra/platform/os/window.hpp>

#include <vri/integration/vri_glfw.h>

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
    if (!glfwInit())
        return 1;
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* glfwWindow = glfwCreateWindow(128, 96, "Swapchain test", nullptr, nullptr);
    if (!glfwWindow)
    {
        glfwTerminate();
        return 1;
    }
    auto result = [&]() -> std::expected<void, std::string>
    {
        auto window = vultra::Window::borrow(glfwWindow);
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
        return vultra::testing::swapchainExpectedProbe(**device,
                                                       **swapchain,
                                                       [&](vultra::Swapchain& swap) -> std::expected<void, std::string>
                                                       {
                                                           glfwSetWindowSize(glfwWindow, 160, 120);
                                                           glfwPollEvents();
                                                           auto resized = (*window)->framebufferSizeExpected();
                                                           if (!resized || resized->empty())
                                                               return std::unexpected(
                                                                   "Resized window has no framebuffer");
                                                           if (auto updated = swap.resizeExpected(*resized); !updated)
                                                               return updated;
                                                           if (auto updated = swap.setVsyncExpected(false); !updated)
                                                               return updated;
                                                           if (auto updated = swap.setVsyncExpected(true); !updated)
                                                               return updated;
                                                           return {};
                                                       });
    }();
    glfwDestroyWindow(glfwWindow);
    glfwTerminate();
    if (!result)
    {
        std::cerr << result.error() << '\n';
        return 1;
    }
    std::cout << "Window and swapchain expected surface presentation passed\n";
    return 0;
}
