#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/render_device.hpp>

using namespace vultra;

int main()
{
    auto window = os::Window::Builder {}.setTitle("Empty Vultra Window").setExtent({1024, 768}).build();

    rhi::RenderDevice renderDevice(
        rhi::RenderDeviceFeatureFlagBits::eNormal, "Empty Vultra Window", window->getRequiredVulkanInstanceExtensions());

    VULTRA_CLIENT_INFO("RenderDevice Name: {}", renderDevice.getName());
    VULTRA_CLIENT_INFO("RenderDevice PhysicalDeviceInfo: {}", renderDevice.getPhysicalDeviceInfo().toString());

    window->setTitle(std::format("Empty Window ({})", renderDevice.getName()));

    // Create swapchain
    rhi::Swapchain swapchain = renderDevice.createSwapchain(*window);

    // Create frame controller
    rhi::FrameController frameController {renderDevice, swapchain, 2};

    while (!window->shouldClose())
    {
        window->pollEvents();

        if (!swapchain)
            continue;

        bool  acquiredNextFrame = frameController.acquireNextFrame();
        if (!acquiredNextFrame)
        {
            continue;
        }
        auto& backBuffer = swapchain.getCurrentBuffer();

        auto& cb = frameController.beginFrame();

        rhi::prepareForAttachment(cb, backBuffer, false);

        const rhi::FramebufferInfo framebufferInfo {
            .area             = rhi::Rect2D {.extent = backBuffer.getExtent()},
            .colorAttachments = {{
                .target     = &backBuffer,
                .clearValue = glm::vec4 {0.2f, 0.3f, 0.3f, 1.0f},
            }},
        };

        {
            RHI_GPU_ZONE(cb, "Empty Window");
            cb.beginRendering(framebufferInfo).endRendering();
        }

        frameController.endFrame();
        frameController.present();
    }

    // Remember to wait idle explicitly before any destructors.
    renderDevice.waitIdle();

    return 0;
}
