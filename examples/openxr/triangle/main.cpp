#include <vultra/core/base/common_context.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>
#include <vultra/function/app/xr_app.hpp>
#include <vultra/function/openxr/xr_device.hpp>
#include <vultra/function/openxr/xr_headset.hpp>

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

class OpenXRExampleApp final : public XRApp
{
public:
    explicit OpenXRExampleApp(const std::span<char*>& args) :
        XRApp(args,
              {.title                   = "OpenXR RHI Triangle with ImGui",
               .renderDeviceFeatureFlag = rhi::RenderDeviceFeatureFlagBits::eOpenXR})
    {
        m_VertexBuffer = m_RenderDevice->createVertexBuffer(sizeof(SimpleVertex), 3);

        // Upload vertex buffer
        {
            constexpr auto kVerticesSize       = sizeof(SimpleVertex) * kTriangle.size();
            auto           stagingVertexBuffer = m_RenderDevice->createStagingBuffer(kVerticesSize, kTriangle.data());

            m_RenderDevice->execute([&](auto& cb) {
                cb.copyBuffer(stagingVertexBuffer, m_VertexBuffer, vk::BufferCopy {0, 0, kVerticesSize});
            });
        }

        m_XrGraphicsPipeline = rhi::GraphicsPipeline::Builder {}
                                   .setColorFormats({rhi::PixelFormat::eRGBA8_sRGB})
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
                                   .build(*m_RenderDevice);
    }

    void onImGui() override
    {
        ImGui::Begin("OpenXR Example");
        ImGui::Text("This is a simple OpenXR example with RHI triangle rendering.");

        const auto& xrDevice             = m_RenderDevice->getXRDevice();
        const auto& xrInstanceProperties = xrDevice->getXrInstanceProperties();
        ImGui::Text("OpenXR Runtime        : %s", xrInstanceProperties.runtimeName);
        ImGui::Text("OpenXR Runtime Version: %d.%d.%d",
                    XR_VERSION_MAJOR(xrInstanceProperties.runtimeVersion),
                    XR_VERSION_MINOR(xrInstanceProperties.runtimeVersion),
                    XR_VERSION_PATCH(xrInstanceProperties.runtimeVersion));

#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            m_WantCaptureFrame = true;
        }
#endif

        ImGui::End();
    }

    void onXrRender(rhi::CommandBuffer&                        cb,
                    openxr::XRHeadset::StereoRenderTargetView& xrRenderTargetView,
                    const fsec                                 dt) override
    {
        {
            rhi::prepareForAttachment(cb, xrRenderTargetView.left, false);
            RHI_GPU_ZONE(cb, "RHI Triangle Left");
            cb.beginRendering({
                                  .area = {.extent = xrRenderTargetView.left.getExtent()},
                                  .colorAttachments =
                                      {
                                          {
                                              .target     = &xrRenderTargetView.left,
                                              .clearValue = glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f},
                                          },
                                      },
                              })
                .bindPipeline(m_XrGraphicsPipeline)
                .draw({
                    .vertexBuffer = &m_VertexBuffer,
                    .numVertices  = static_cast<uint32_t>(kTriangle.size()),
                })
                .endRendering();
        }

        {
            rhi::prepareForAttachment(cb, xrRenderTargetView.right, false);
            RHI_GPU_ZONE(cb, "RHI Triangle Right");
            cb.beginRendering({
                                  .area = {.extent = xrRenderTargetView.right.getExtent()},
                                  .colorAttachments =
                                      {
                                          {
                                              .target     = &xrRenderTargetView.right,
                                              .clearValue = glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f},
                                          },
                                      },
                              })
                .bindPipeline(m_XrGraphicsPipeline)
                .draw({
                    .vertexBuffer = &m_VertexBuffer,
                    .numVertices  = static_cast<uint32_t>(kTriangle.size()),
                })
                .endRendering();
        }
    }

private:
    rhi::VertexBuffer     m_VertexBuffer;
    rhi::GraphicsPipeline m_XrGraphicsPipeline;
};

CONFIG_MAIN(OpenXRExampleApp)