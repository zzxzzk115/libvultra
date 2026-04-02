#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/render_device.hpp>
#include <vultra/core/rhi/render_pass.hpp>
#include <vultra/function/framegraph/framegraph_context.hpp>
#include <vultra/function/framegraph/transient_resources.hpp>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#if _DEBUG
#include <fstream>
#endif

struct SimpleVertex
{
    glm::vec3 position;
    glm::vec3 color;
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

// Triangle in NDC for simplicity.
constexpr auto kTriangle = std::array {
    // clang-format off
    //                    position                 color
    SimpleVertex{ {  0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // top
    SimpleVertex{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, // left
    SimpleVertex{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }  // right
    // clang-format on
};

using namespace vultra;

class TriangleSinglePass final : public rhi::RenderPass<TriangleSinglePass>
{
public:
    explicit TriangleSinglePass(rhi::RenderDevice& rd) : m_RenderDevice(rd)
    {
        // Create vertex buffer
        m_VertexBuffer = std::move(rd.createVertexBuffer(sizeof(SimpleVertex), 3));

        // Upload vertex buffer
        {
            constexpr auto kVerticesSize       = sizeof(SimpleVertex) * kTriangle.size();
            auto           stagingVertexBuffer = rd.createStagingBuffer(kVerticesSize, kTriangle.data());

            rd.execute([&](auto& cb) {
                cb.copyBuffer(stagingVertexBuffer, m_VertexBuffer, rhi::BufferCopy {0, 0, kVerticesSize});
            });
        }
    }

    void addPass(FrameGraph& fg, const rhi::PixelFormat& swapchainPixelFormat)
    {
        fg.addCallbackPass(
            "TriangleSinglePass",
            [this](FrameGraph::Builder& builder, auto&) { builder.setSideEffect(); },
            [this, swapchainPixelFormat](const auto&, auto&, void* ctx) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctx);
                auto  fb = rc.framebufferInfo();
                if (!fb)
                    return;

                RHI_GPU_ZONE(rc.cb, "TriangleSinglePass");

                const auto* pipeline = ensurePipeline(swapchainPixelFormat);
                if (!pipeline)
                    return;

                rc.cb.beginRendering(*fb)
                    .bindPipeline(*pipeline)
                    .draw({
                        .vertexBuffer = &m_VertexBuffer,
                        .numVertices  = static_cast<uint32_t>(kTriangle.size()),
                    })
                    .endRendering();
            });
    }

private:
    [[nodiscard]] const rhi::GraphicsPipeline* ensurePipeline(const rhi::PixelFormat format)
    {
        if (!m_GraphicsPipeline)
            m_GraphicsPipeline = createPipeline(format);
        return m_GraphicsPipeline ? &m_GraphicsPipeline : nullptr;
    }

    [[nodiscard]] rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat& format) const
    {
        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({format})
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
            .build(m_RenderDevice);
    }

private:
    rhi::RenderDevice&    m_RenderDevice;
    rhi::VertexBuffer     m_VertexBuffer;
    rhi::GraphicsPipeline m_GraphicsPipeline;
};

int main()
{
    auto window = os::Window::Builder {}.setExtent({1024, 768}).build();

    // Event callback
    window->on<os::GeneralWindowEvent>([](const os::GeneralWindowEvent& event, os::Window& wd) {
        if (event.type == vultra::event::WindowEventType::eKeyDown && event.key.has_value())
        {
            // Press ESC to close the window
            if (event.key->key == KeyCode::eEscape)
            {
                wd.close();
            }
        }
    });

    rhi::RenderDevice renderDevice(rhi::RenderDeviceFeatureFlagBits::eNormal,
                                   "FrameGraph Triangle",
                                   window->getRequiredVulkanInstanceExtensions());

    VULTRA_CLIENT_INFO("RenderDevice Name: {}", renderDevice.getName());
    VULTRA_CLIENT_INFO("RenderDevice PhysicalDeviceInfo: {}", renderDevice.getPhysicalDeviceInfo().toString());

    VULTRA_CLIENT_WARN("Press ESC to close the window");

    window->setTitle(std::format("FrameGraph Triangle ({})", renderDevice.getName()));

    // Create swapchain
    rhi::Swapchain swapchain = renderDevice.createSwapchain(*window);

    // Create frame controller
    rhi::FrameController frameController {renderDevice, swapchain, 3};

    // Create transient resources
    framegraph::TransientResources transientResources {renderDevice};

    // Create render pass
    TriangleSinglePass triangleSinglePass {renderDevice};

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

        // Prepare backBuffer for rendering
        rhi::prepareForAttachment(cb, backBuffer, false);

        // Create FrameGraph
        FrameGraph fg;

        // Add passes
        triangleSinglePass.addPass(fg, swapchain.getPixelFormat());

        // Compile the frame graph
        fg.compile();

        // Execute the frame graph
        {
            FrameRenderData frameData {};
            ViewRenderData  viewData {};

            viewData.view.extent = swapchain.getExtent();
            viewData.framebufferInfo = rhi::FramebufferInfo {
                .area = rhi::Rect2D {.extent = swapchain.getExtent()},
                .colorAttachments =
                    {
                        {
                            .target     = &backBuffer,
                            .clearValue = glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f},
                        },
                    },
            };
            FrameGraphExecContext rc {
                .cb          = cb,
                .rd          = renderDevice,
                .frame       = frameData,
                .viewData    = viewData,
                .resourceSet = {},
                .ext         = {.builtinShaderLib = nullptr, .samplers = {}},
            };

            FG_GPU_ZONE(rc.cb);
            fg.execute(&rc, &transientResources);
        }

#if _DEBUG
        {
            std::ofstream ofs {"framegraph.dot"};
            ofs << fg;
        }
#endif

        transientResources.update();

        frameController.endFrame();
        frameController.present();
    }

    // Remember to wait idle explicitly before any destructors.
    renderDevice.waitIdle();

    return 0;
}
