#include <vultra/core/base/common_context.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/renderer/imgui_renderer.hpp>

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
        ImGuiApp(args,
                 {.title = "RHI Triangle with ImGui", .vSyncConfig = rhi::VerticalSync::eEnabled},
                 {.enableDocking = false})
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

    ~ImGuiExampleApp() override
    {
        if (m_TextureID != nullptr)
            imgui::removeTexture(*m_RenderDevice, m_TextureID);
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
        if (m_TextureID != nullptr)
        {
            bool open = false;
            if (ImGui::Button("Show Render Target"))
            {
                open = true;
            }
            imgui::textureViewer("Render Target Viewer",
                                 m_TextureID,
                                 m_Texture,
                                 m_TextureSize,
                                 "rendertarget.png",
                                 *m_RenderDevice,
                                 open);
        }

        ImGui::End();
    }

    void onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt) override
    {
        const auto& [frameIndex, target] = rtv;
        // rhi::prepareForAttachment(cb, target, false);
        // Add to imgui texture
        if (m_TextureID == nullptr || m_Texture.getExtent() != target.getExtent())
        {
            m_Texture = rhi::Texture::Builder {}
                            .setExtent(target.getExtent())
                            .setPixelFormat(target.getPixelFormat())
                            .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                           rhi::ImageUsage::eTransfer)
                            .setupOptimalSampler(true)
                            .build(*m_RenderDevice);

            m_TextureID = imgui::addTexture(m_Texture);
            m_TextureSize =
                ImVec2 {static_cast<float>(target.getExtent().width), static_cast<float>(target.getExtent().height)};
        }
        rhi::prepareForAttachment(cb, m_Texture, false);
        {
            RHI_GPU_ZONE(cb, "RHI Triangle");
            cb.beginRendering({
                                  .area = {.extent = m_Texture.getExtent()},
                                  .colorAttachments =
                                      {
                                          {
                                              .target     = &m_Texture,
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
            rhi::prepareForReading(cb, m_Texture);
            cb.blit(m_Texture, rtv.texture, vk::Filter::eLinear);
            rhi::prepareForReading(cb, m_Texture);
        }
        ImGuiApp::onRender(cb, rtv, dt);
    }

private:
    rhi::VertexBuffer     m_VertexBuffer;
    rhi::GraphicsPipeline m_GraphicsPipeline;

    rhi::Texture          m_Texture;
    imgui::ImGuiTextureID m_TextureID {nullptr};
    ImVec2                m_TextureSize {0.0f, 0.0f};
};

CONFIG_MAIN(ImGuiExampleApp)