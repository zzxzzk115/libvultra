#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/command_buffer.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/render_device.hpp>
#include <vultra/core/rhi/structs/render_backend_api.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

using namespace vultra;

int main()
try
{
    auto window = os::Window::Builder {}.setTitle("RHI Triangle (WebGPU)").setExtent({1024, 768}).build();

    window->on<os::GeneralWindowEvent>([](const os::GeneralWindowEvent& event, os::Window& wd) {
        if (event.type == vultra::event::WindowEventType::eKeyDown && event.key.has_value())
        {
            if (event.key->key == KeyCode::eEscape)
            {
                wd.close();
            }
        }
    });

    rhi::RenderDevice renderDevice(rhi::RenderDeviceFeatureFlagBits::eNormal,
                                   "RHI Triangle WebGPU",
                                   std::span<const char* const> {},
                                   rhi::RenderBackendApi::eWebGPU);

    VULTRA_CLIENT_INFO("RenderDevice Name: {}", renderDevice.getName());
    VULTRA_CLIENT_INFO("RenderDevice PhysicalDeviceInfo: {}", renderDevice.getPhysicalDeviceInfo().toString());
    VULTRA_CLIENT_INFO("Backend API: {}", static_cast<int>(renderDevice.getBackendApi()));

#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
    VULTRA_CLIENT_WARN("This build disables WebGPU. Rebuild with VULTRA_ENABLE_WEBGPU=1.");
    return 0;
#else
    auto swapchain = renderDevice.createSwapchain(*window, rhi::SwapchainFormat::esRGB, rhi::VerticalSync::eEnabled);
    rhi::FrameController frameController {renderDevice, swapchain, 3};

    constexpr std::string_view kTriangleVertWGSL = R"(
struct VSOut {
    @builtin(position) position : vec4f,
    @location(0) color : vec3f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertex_index : u32) -> VSOut {
    let positions = array<vec2f, 3>(
        vec2f(0.0, 0.5),
        vec2f(-0.5, -0.5),
        vec2f(0.5, -0.5)
    );
    let colors = array<vec3f, 3>(
        vec3f(1.0, 0.0, 0.0),
        vec3f(0.0, 1.0, 0.0),
        vec3f(0.0, 0.0, 1.0)
    );

    var out : VSOut;
    out.position = vec4f(positions[vertex_index], 0.0, 1.0);
    out.color = colors[vertex_index];
    return out;
}
)";
    constexpr std::string_view kTriangleFragWGSL = R"(
@fragment
fn fs_main(@location(0) color : vec3f) -> @location(0) vec4f {
    return vec4f(color, 1.0);
}
)";

    auto graphicsPipeline =
        rhi::GraphicsPipeline::Builder {}
            .setColorFormats({swapchain.getPixelFormat()})
            .addShader(rhi::ShaderType::eVertex, {.code = std::string(kTriangleVertWGSL), .entryPointName = "vs_main"})
            .addShader(rhi::ShaderType::eFragment,
                       {.code = std::string(kTriangleFragWGSL), .entryPointName = "fs_main"})
            .setDepthStencil({
                .depthTest  = false,
                .depthWrite = false,
            })
            .setRasterizer({.polygonMode = rhi::PolygonMode::eFill})
            .setBlending(0, {.enabled = false})
            .build(renderDevice);
    if (!graphicsPipeline)
    {
        throw std::runtime_error("Failed to create RHI graphics pipeline");
    }

    while (!window->shouldClose())
    {
        window->pollEvents();
        if (!swapchain)
        {
            continue;
        }
        if (!frameController.acquireNextFrame())
        {
            continue;
        }

        auto&                      backBuffer = swapchain.getCurrentBuffer();
        const rhi::FramebufferInfo framebufferInfo {
            .area = rhi::Rect2D {.extent = backBuffer.getExtent()},
            .colorAttachments =
                {
                    {
                        .target     = &backBuffer,
                        .clearValue = glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
        };

        auto& cb = frameController.beginFrame();
        {
            RHI_GPU_ZONE(cb, "RHI Triangle WebGPU");
            cb.beginRendering(framebufferInfo)
                .bindPipeline(graphicsPipeline)
                .draw({
                    .numVertices = 3u,
                })
                .endRendering();
        }
        frameController.endFrame();
        frameController.present();
    }

    renderDevice.waitIdle();
    return 0;
#endif
}
catch (const std::exception& e)
{
    VULTRA_CLIENT_CRITICAL("Exception: {}", e.what());
    return 1;
}
