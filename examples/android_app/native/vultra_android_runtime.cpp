#include <array>

#include <glm/glm.hpp>

#include "android_runtime_context.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/rhi/command_buffer.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/render_device.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>
#include <vultra/platform/android/android_window.hpp>

using namespace vultra;
using vultra::platform::android::AndroidWindow;

namespace
{
    struct SimpleVertex
    {
        glm::vec3 position;
        glm::vec3 color;
    };

    void logException(const std::exception& e) { VULTRA_CORE_ERROR("[AndroidRuntime] Exception: {}", e.what()); }
}

extern "C" void vultra_android_run(const VultraAndroidRuntimeContext* runtimeContext)
try
{
    VULTRA_CORE_INFO("[AndroidRuntime] Starting Android runtime");
    AndroidWindow window {runtimeContext->nativeWindow, runtimeContext->destroyRequested};
    bool          loggedReady  = false;
    bool          initialized  = false;

    std::optional<rhi::RenderDevice>      renderDevice;
    std::optional<rhi::Swapchain>         swapchain;
    std::optional<rhi::FrameController>   frameController;
    std::optional<rhi::VertexBuffer>      vertexBuffer;
    std::optional<rhi::GraphicsPipeline>  graphicsPipeline;

    constexpr auto kTriangle = std::array {
        SimpleVertex {{0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        SimpleVertex {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        SimpleVertex {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    };

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

    while (!window.shouldClose())
    {
        const int pollTimeout = (!loggedReady && !window.isReady()) ? -1 : 0;
        (void)window.pollEvents(pollTimeout);

        if (!window.isReady())
        {
            continue;
        }

        if (!loggedReady)
        {
            const auto extent = window.extent();
            VULTRA_CORE_INFO("[AndroidRuntime] Native window ready: {}x{}", extent.x, extent.y);
            loggedReady = true;
        }

        if (!initialized)
        {
            renderDevice.emplace(rhi::RenderDeviceFeatureFlagBits::eNormal);
            swapchain.emplace(renderDevice->createSwapchain(window));
            frameController.emplace(*renderDevice, *swapchain, 3);

            vertexBuffer.emplace(renderDevice->createVertexBuffer(sizeof(SimpleVertex), kTriangle.size()));
            {
                constexpr auto kVerticesSize = sizeof(SimpleVertex) * kTriangle.size();
                auto           stagingVertexBuffer = renderDevice->createStagingBuffer(kVerticesSize, kTriangle.data());
                renderDevice->execute([&](auto& cb) {
                    cb.copyBuffer(stagingVertexBuffer, *vertexBuffer, vk::BufferCopy {0, 0, kVerticesSize});
                });
            }

            graphicsPipeline.emplace(
                rhi::GraphicsPipeline::Builder {}
                    .setColorFormats({swapchain->getPixelFormat()})
                    .setInputAssembly({
                        {0, {.type = rhi::VertexAttribute::Type::eFloat3, .offset = 0}},
                        {1, {.type = rhi::VertexAttribute::Type::eFloat3, .offset = offsetof(SimpleVertex, color)}},
                    })
                    .addShader(rhi::ShaderType::eVertex, {.code = vertCode})
                    .addShader(rhi::ShaderType::eFragment, {.code = fragCode})
                    .setDepthStencil({.depthTest = false, .depthWrite = false})
                    .setRasterizer({.polygonMode = rhi::PolygonMode::eFill})
                    .setBlending(0, {.enabled = false})
                    .build(*renderDevice));
            VULTRA_CORE_INFO("[AndroidRuntime] Triangle renderer initialized");
            initialized = true;
        }

        auto& backBuffer        = frameController->getCurrentTarget().texture;
        bool  acquiredNextFrame = frameController->acquireNextFrame();
        if (!acquiredNextFrame)
        {
            continue;
        }

        auto& cb = frameController->beginFrame();
        rhi::prepareForAttachment(cb, backBuffer, false);
        const rhi::FramebufferInfo framebufferInfo {.area             = rhi::Rect2D {.extent = backBuffer.getExtent()},
                                                    .colorAttachments = {{
                                                        {.target = &backBuffer, .clearValue = glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f}},
                                                    }}};
        cb.beginRendering(framebufferInfo)
            .bindPipeline(*graphicsPipeline)
            .draw({.vertexBuffer = &*vertexBuffer, .numVertices = static_cast<uint32_t>(kTriangle.size())})
            .endRendering();

        frameController->endFrame();
        frameController->present();
    }

    if (renderDevice.has_value())
    {
        renderDevice->waitIdle();
    }

    VULTRA_CORE_INFO("[AndroidRuntime] Shutting down Android runtime");
}
catch (const std::exception& e)
{
    logException(e);
}
