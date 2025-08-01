#include <vultra/core/base/common_context.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>
#include <vultra/function/app/imgui_app.hpp>

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

class ImGuiExampleApp final : public ImGuiApp
{
public:
    explicit ImGuiExampleApp(const std::span<char*>& args) :
        ImGuiApp(args, {.title = "RHI Triangle with ImGui"}, {.enableDocking = false})
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

        m_GraphicsPipeline = rhi::GraphicsPipeline::Builder {}
                                 .setColorFormats({m_Swapchain.getPixelFormat()})
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
        ImGui::ShowDemoWindow();
        ImGui::Begin("Example Window");
        ImGui::Text("Hello, world!");
#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            m_WantCaptureFrame = true;
        }
#endif
        ImGui::End();
    }

    void onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt) override
    {
        const auto& [frameIndex, target] = rtv;
        rhi::prepareForAttachment(cb, target, false);
        {
            RHI_GPU_ZONE(cb, "RHI Triangle");
            cb.beginRendering({
                                  .area = {.extent = target.getExtent()},
                                  .colorAttachments =
                                      {
                                          {
                                              .target     = &target,
                                              .clearValue = glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f},
                                          },
                                      },
                              })
                .bindPipeline(m_GraphicsPipeline)
                .draw({
                    .vertexBuffer = &m_VertexBuffer,
                    .numVertices  = static_cast<uint32_t>(kTriangle.size()),
                })
                .endRendering();
        }
        ImGuiApp::onRender(cb, rtv, dt);
    }

private:
    rhi::VertexBuffer     m_VertexBuffer;
    rhi::GraphicsPipeline m_GraphicsPipeline;
};

CONFIG_MAIN(ImGuiExampleApp)