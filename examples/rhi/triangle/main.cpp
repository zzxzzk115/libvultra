#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/command_buffer.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/render_device.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>

using namespace vultra;

struct SimpleVertex
{
    glm::vec3 position;
    glm::vec3 color;
};

int main()
try
{
    os::Window window = os::Window::Builder {}.setExtent({1024, 768}).build();

    // Event callback
    window.on<os::GeneralWindowEvent>([](const os::GeneralWindowEvent& event, os::Window& wd) {
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            // Press ESC to close the window
            if (event.internalEvent.key.key == SDLK_ESCAPE)
            {
                wd.close();
            }
        }
    });

    rhi::RenderDevice renderDevice(rhi::RenderDeviceFeatureFlagBits::eNormal);

    VULTRA_CLIENT_INFO("RenderDevice Name: {}", renderDevice.getName());
    VULTRA_CLIENT_INFO("RenderDevice PhysicalDeviceInfo: {}", renderDevice.getPhysicalDeviceInfo().toString());

    VULTRA_CLIENT_WARN("Press ESC to close the window");

    window.setTitle(std::format("RHI Triangle ({})", renderDevice.getName()));

    // Create swapchain
    rhi::Swapchain swapchain = renderDevice.createSwapchain(window);

    // Create frame controller
    rhi::FrameController frameController {renderDevice, swapchain, 3};

    // Create vertex buffer
    rhi::VertexBuffer vertexBuffer = renderDevice.createVertexBuffer(sizeof(SimpleVertex), 3);

    // Upload vertex buffer
    // Triangle in NDC for simplicity.
    constexpr auto kTriangle = std::array {
        // clang-format off
        //                    position                 color
        SimpleVertex{ {  0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // top
        SimpleVertex{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, // left
        SimpleVertex{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }  // right
        // clang-format on
    };
    {
        constexpr auto kVerticesSize       = sizeof(SimpleVertex) * kTriangle.size();
        auto           stagingVertexBuffer = renderDevice.createStagingBuffer(kVerticesSize, kTriangle.data());

        renderDevice.execute(
            [&](auto& cb) { cb.copyBuffer(stagingVertexBuffer, vertexBuffer, vk::BufferCopy {0, 0, kVerticesSize}); });
    }

    const auto* const vertCode = R"(
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Color;

out gl_PerVertex { vec4 gl_Position; };
layout(location = 0) out vec3 v_FragColor;

void main() {
  v_FragColor = a_Color;
  gl_Position = vec4(a_Position, 1.0);
  gl_Position.y *= -1.0;
})";
    const auto* const fragCode = R"(
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 v_FragColor;
layout(location = 0) out vec4 FragColor;

void main() {
  FragColor = vec4(v_FragColor, 1.0);
})";

    // Create graphics pipeline
    auto graphicsPipeline = rhi::GraphicsPipeline::Builder {}
                                .setColorFormats({swapchain.getPixelFormat()})
                                .setInputAssembly({
                                    {0, {.type = rhi::VertexAttribute::Type::eFloat3, .offset = 0}},
                                    {1,
                                     {
                                         .type   = rhi::VertexAttribute::Type::eFloat3,
                                         .offset = offsetof(SimpleVertex, color),
                                     }},
                                })
                                .addShader(rhi::ShaderType::eVertex, {.code = vertCode})
                                .addShader(rhi::ShaderType::eFragment, {.code = fragCode})
                                .setDepthStencil({
                                    .depthTest  = false,
                                    .depthWrite = false,
                                })
                                .setRasterizer({.polygonMode = rhi::PolygonMode::eFill})
                                .setBlending(0, {.enabled = false})
                                .build(renderDevice);

    while (!window.shouldClose())
    {
        window.pollEvents();

        if (!swapchain)
            continue;

        auto& backBuffer        = frameController.getCurrentTarget().texture;
        auto& cb                = frameController.beginFrame();
        bool  acquiredNextFrame = frameController.acquireNextFrame();
        if (!acquiredNextFrame)
        {
            continue;
        }

        rhi::prepareForAttachment(cb, backBuffer, false);
        const rhi::FramebufferInfo framebufferInfo {.area             = rhi::Rect2D {.extent = backBuffer.getExtent()},
                                                    .colorAttachments = {
                                                        {
                                                            .target     = &backBuffer,
                                                            .clearValue = glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f},
                                                        },
                                                    }};
        {
            RHI_GPU_ZONE(cb, "RHI Triangle");
            cb.beginRendering(framebufferInfo)
                .bindPipeline(graphicsPipeline)
                .draw({
                    .vertexBuffer = &vertexBuffer,
                    .numVertices  = static_cast<uint32_t>(kTriangle.size()),
                })
                .endRendering();
        }

        frameController.endFrame();
        frameController.present();
    }

    // Remember to wait idle explicitly before any destructors.
    renderDevice.waitIdle();

    return 0;
}
catch (const std::exception& e)
{
    VULTRA_CLIENT_CRITICAL("Exception: {}", e.what());
}