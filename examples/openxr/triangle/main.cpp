#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>
#include <vultra/function/openxr/xr_device.hpp>
#include <vultra/function/rendering/srp/renderer.hpp>
#include <vultra/function/services/frame_debugger_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>

#include <imgui.h>

using namespace vultra;

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

class OpenXRTriangleRenderer final : public Renderer
{
public:
    std::string_view name() const override { return "openxr_triangle_renderer"; }

    void init() override
    {
        auto& backendService = getServices()->require<IRenderBackendService>();
        auto& renderDevice   = backendService.renderDevice();

        m_VertexBuffer = renderDevice.createVertexBuffer(sizeof(SimpleVertex), 3);

        // Upload vertex buffer
        {
            constexpr auto kVerticesSize       = sizeof(SimpleVertex) * kTriangle.size();
            auto           stagingVertexBuffer = renderDevice.createStagingBuffer(kVerticesSize, kTriangle.data());

            renderDevice.execute([&](auto& cb) {
                cb.copyBuffer(stagingVertexBuffer, m_VertexBuffer, rhi::BufferCopy {0, 0, kVerticesSize});
            });
        }

        auto createPipelineForFormat = [&](const rhi::PixelFormat colorFormat) {
            return rhi::GraphicsPipeline::Builder {}
                .setColorFormats({colorFormat})
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
        };

        m_XrGraphicsPipeline   = createPipelineForFormat(rhi::PixelFormat::eRGBA8_sRGB);
        m_SwapchainPipeline    = createPipelineForFormat(rhi::PixelFormat::eBGRA8_sRGB);
        m_FallbackSrgbPipeline = createPipelineForFormat(rhi::PixelFormat::eRGBA8_sRGB);
    }

    void render(ImmediateRenderContext& ctx) override
    {
        auto& cb     = ctx.cb;
        auto* target = ctx.view().target;
        if (!target)
            return;

        const rhi::GraphicsPipeline* pipeline = &m_FallbackSrgbPipeline;
        if (target->getPixelFormat() == rhi::PixelFormat::eBGRA8_sRGB)
            pipeline = &m_SwapchainPipeline;
        else if (target->getPixelFormat() == rhi::PixelFormat::eRGBA8_sRGB)
            pipeline = &m_XrGraphicsPipeline;

        rhi::prepareForAttachment(cb, *target, false);
        RHI_GPU_ZONE(cb, "OpenXR Triangle");
        cb.beginRendering({
                              .area = {.extent = target->getExtent()},
                              .colorAttachments =
                                  {
                                      {
                                          .target     = target,
                                          .clearValue = glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f},
                                      },
                                  },
                          })
            .bindPipeline(*pipeline)
            .draw({
                .vertexBuffer = &m_VertexBuffer,
                .numVertices  = static_cast<uint32_t>(kTriangle.size()),
            })
            .endRendering();
    }

    void onImGui() override
    {
        ImGui::Begin("OpenXR Triangle Example", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("DemoAppHost + OpenXR render backend");

        auto& backendService = getServices()->require<IRenderBackendService>();
        ImGui::Text("XR Enabled : %s", backendService.isXREnabled() ? "Yes" : "No");
        ImGui::Text("XR Mirror Mode Enabled : %s", backendService.isXRMirrorEnabled() ? "Yes" : "No");

        auto* xrDevice = backendService.renderDevice().getXRDevice();
        if (xrDevice)
        {
            const auto& xrInstanceProperties = xrDevice->getXrInstanceProperties();
            ImGui::Text("OpenXR Runtime        : %s", xrInstanceProperties.runtimeName);
            ImGui::Text("OpenXR Runtime Version: %d.%d.%d",
                        XR_VERSION_MAJOR(xrInstanceProperties.runtimeVersion),
                        XR_VERSION_MINOR(xrInstanceProperties.runtimeVersion),
                        XR_VERSION_PATCH(xrInstanceProperties.runtimeVersion));
        }

#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            getServices()->require<IFrameDebuggerService>().captureSingleFrame();
        }
#endif

        ImGui::End();
    }

private:
    rhi::VertexBuffer     m_VertexBuffer;
    rhi::GraphicsPipeline m_XrGraphicsPipeline;
    rhi::GraphicsPipeline m_SwapchainPipeline;
    rhi::GraphicsPipeline m_FallbackSrgbPipeline;
};

class OpenXRExampleApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "OpenXR RHI Triangle with ImGui"; }

    rhi::RenderDeviceFeatureFlagBits demoRenderDeviceFeatureFlag() const override
    {
        return rhi::RenderDeviceFeatureFlagBits::eXR;
    }

    Ref<Renderer> makeRenderer() const override { return createRef<OpenXRTriangleRenderer>(); }
};

int main(int argc, char** argv)
{
    OpenXRExampleApp app {};
    return app.run(argc, argv);
}
