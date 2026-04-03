#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/backends/webgpu/webgpu_imgui.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/render_device.hpp>
#include <vultra/core/rhi/structs/render_backend_api.hpp>

#include <imgui.h>

using namespace vultra;

int main()
try
{
    auto window = os::Window::Builder {}.setTitle("ImGui Example (WebGPU)").setExtent({1024, 768}).build();

    rhi::RenderDevice renderDevice(rhi::RenderDeviceFeatureFlagBits::eNormal,
                                   "ImGui Example WebGPU",
                                   std::span<const char* const> {},
                                   rhi::RenderBackendApi::eWebGPU);

    auto swapchain = renderDevice.createSwapchain(*window, rhi::SwapchainFormat::esRGB, rhi::VerticalSync::eEnabled);
    rhi::FrameController frameController {renderDevice, swapchain, 2};

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    rhi::WebGPUImGui imguiBackend {renderDevice};
    imguiBackend.init(*window, renderDevice, swapchain, false, false);

    window->on<os::GeneralWindowEvent>([&](const os::GeneralWindowEvent& event, os::Window& wd) {
        imguiBackend.processEvent(event);
        if (event.type == vultra::event::WindowEventType::eKeyDown && event.key.has_value() &&
            event.key->key == KeyCode::eEscape)
        {
            wd.close();
        }
    });

    while (!window->shouldClose())
    {
        window->pollEvents();
        if (!swapchain || !frameController.acquireNextFrame())
        {
            continue;
        }

        imguiBackend.beginFrame(*window);
        ImGui::NewFrame();

        ImGui::ShowDemoWindow();
        ImGui::Begin("WebGPU ImGui");
        ImGui::Text("Hello from WebGPU + ImGui backend.");
        ImGui::End();

        auto& backBuffer = swapchain.getCurrentBuffer();
        const rhi::FramebufferInfo framebufferInfo {
            .area = rhi::Rect2D {.extent = backBuffer.getExtent()},
            .colorAttachments =
                {
                    {
                        .target     = &backBuffer,
                        .clearValue = glm::vec4 {0.1f, 0.1f, 0.1f, 1.0f},
                    },
                },
        };

        auto& cb = frameController.beginFrame();
        cb.beginRendering(framebufferInfo);
        imguiBackend.render(cb);
        cb.endRendering();

        frameController.endFrame();
        frameController.present();
        imguiBackend.postRender();
    }

    renderDevice.waitIdle();
    imguiBackend.shutdown({}, nullptr);
    ImGui::DestroyContext();
    return 0;
}
catch (const std::exception& e)
{
    VULTRA_CLIENT_CRITICAL("Exception: {}", e.what());
    return 1;
}
