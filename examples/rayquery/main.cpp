#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input.hpp>
#include <vultra/core/rhi/raytracing/raytracing_pipeline.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/renderer/mesh_manager.hpp>
#include <vultra/function/renderer/vertex_format.hpp>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace vultra;

constexpr glm::mat4 kTransform = glm::mat4(1.0f);

const char* const vertexCode = R"(
#version 460 core

layout (location = 0) in vec3 a_Position;
layout (location = 1) in vec3 a_Color;
layout (location = 2) in vec3 a_Normal;

layout (location = 0) out vec3 v_Color;
layout (location = 1) out vec3 v_Normal;
layout (location = 2) out vec3 v_FragPos;

layout (push_constant) uniform GlobalPushConstants
{
    mat4 model;
    mat4 viewProjection;
    vec4 lightPos; // w component unused
    vec4 cameraPos; // w component unused
};

void main() {
    v_Color = a_Color;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    v_Normal = normalize(normalMatrix * a_Normal);
    v_FragPos = vec3(model * vec4(a_Position, 1.0));
    gl_Position = viewProjection * vec4(v_FragPos, 1.0);
}
)";

const char* const fragmentCode = R"(
#version 460 core
#extension GL_EXT_ray_query : enable

layout (location = 0) in vec3 v_Color;
layout (location = 1) in vec3 v_Normal;
layout (location = 2) in vec3 v_FragPos;

layout (location = 0) out vec4 FragColor;

layout (set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout (push_constant) uniform GlobalPushConstants
{
    mat4 model;
    mat4 viewProjection;
    vec4 lightPos; // w component unused
    vec4 cameraPos; // w component unused
};

#define ambient 0.1

void main() {
    vec3 N = normalize(v_Normal);
	vec3 L = normalize(lightPos.xyz - v_FragPos);
	vec3 V = normalize(-cameraPos.xyz + v_FragPos);
	vec3 diffuse = max(dot(N, L), ambient) * v_Color;

	FragColor = vec4(diffuse, 1.0);

    float dist = length(lightPos.xyz - v_FragPos);
    vec3 rayOrigin = v_FragPos + N * 1e-6; // Offset a bit to avoid self-intersection
    vec3 rayDirection = L;
    float tMin = 1e-6;
    float tMax = dist - 1e-3;

	rayQueryEXT rayQuery;
	rayQueryInitializeEXT(rayQuery, topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, rayOrigin, tMin, rayDirection, tMax);

	// Traverse the acceleration structure and store information about the first intersection (if any)
	rayQueryProceedEXT(rayQuery);

	// If the intersection has hit a triangle, the fragment is shadowed
	if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
		FragColor *= 0.1;
	}
}
)";

class RayQueryApp final : public ImGuiApp
{
public:
    explicit RayQueryApp(const std::span<char*>& args) :
        ImGuiApp(args,
                 {.title                   = "Ray Query Example",
                  .renderDeviceFeatureFlag = rhi::RenderDeviceFeatureFlagBits::eRayQuery,
                  .vSyncConfig             = rhi::VerticalSync::eEnabled},
                 {.enableDocking = false})
    {
        // Load raytracing shadow model for demonstration
        m_MeshResource =
            resource::loadResource<gfx::MeshManager>("resources/models/raytracing_shadow/raytracing_shadow.gltf");

        // Create and build TLAS
        m_TLAS = m_RenderDevice->createBuildTLAS(m_MeshResource->renderMesh.blas, kTransform);

        // Create graphics pipeline
        m_Pipeline =
            rhi::GraphicsPipeline::Builder {}
                .setDepthFormat(rhi::PixelFormat::eDepth24_Stencil8)
                .setColorFormats({m_Swapchain.getPixelFormat()})
                .setDepthStencil({.depthTest = true, .depthWrite = true, .depthCompareOp = rhi::CompareOp::eLess})
                .setInputAssembly(gfx::SimpleVertex::getVertexFormat()->getAttributes())
                .setBlending(0, {.enabled = false})
                .setTopology(rhi::PrimitiveTopology::eTriangleList)
                .addShader(rhi::ShaderType::eVertex, {.code = vertexCode})
                .addShader(rhi::ShaderType::eFragment, {.code = fragmentCode})
                .build(*m_RenderDevice);

        // Create depth texture
        const auto&   extent = m_Window.getExtent();
        rhi::Extent2D extent2D {static_cast<uint32_t>(extent.x), static_cast<uint32_t>(extent.y)};
        m_DepthTexture = rhi::Texture::Builder {}
                             .setExtent(extent2D)
                             .setPixelFormat(rhi::PixelFormat::eDepth24_Stencil8)
                             .setNumMipLevels(1)
                             .setNumLayers(std::nullopt)
                             .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                            rhi::ImageUsage::eTransferSrc)
                             .build(*m_RenderDevice);
    }

    void onImGui() override
    {
        ImGui::Begin("RayQuery Example");
#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            m_WantCaptureFrame = true;
        }
#endif
        ImGui::End();
    }

    void onUpdate(const fsec dt) override
    {
        // Close on Escape
        if (Input::getKeyDown(KeyCode::eEscape))
        {
            close();
        }

        ImGuiApp::onUpdate(dt);
    }

    void onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt) override
    {
        // Skip rendering if resizing is not finished
        if (rtv.texture.getExtent() != m_DepthTexture.getExtent())
        {
            VULTRA_CLIENT_TRACE("RTV size ({}, {}) != Depth Texture size ({}, {}), skipping rendering this frame",
                                rtv.texture.getExtent().width,
                                rtv.texture.getExtent().height,
                                m_DepthTexture.getExtent().width,
                                m_DepthTexture.getExtent().height);
            ImGuiApp::onRender(cb, rtv, dt);
            return;
        }

        // Record graphics commands
        rhi::prepareForAttachment(cb, rtv.texture, false);
        rhi::prepareForAttachment(cb, m_DepthTexture, false);

        auto descriptorSet = cb.createDescriptorSetBuilder()
                                 .bind(0, rhi::bindings::AccelerationStructureKHR {.as = &m_TLAS})
                                 .build(m_Pipeline.getDescriptorSetLayout(0));

        const auto& extent2D = rtv.texture.getExtent();

        struct GlobalPushConstants
        {
            glm::mat4 model;
            glm::mat4 viewProjection;
            glm::vec4 lightPos;  // w component unused
            glm::vec4 cameraPos; // w component unused
        };

        static glm::vec3 lightPos {-5.0f, 5.0f, -5.0f};
        static glm::vec3 camPos = glm::vec3(0.0f, 4.0f, 8.0f);
        glm::mat4        projection =
            glm::perspective(glm::radians(45.0f),
                             static_cast<float>(m_Window.getExtent().x) / static_cast<float>(m_Window.getExtent().y),
                             0.1f,
                             100.0f);
        projection[1][1] *= -1; // Flip Y for Vulkan
        glm::mat4 view     = glm::lookAt(camPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 viewProj = projection * view;

        GlobalPushConstants pushConstants {kTransform, viewProj, glm::vec4(lightPos, 1.0f), glm::vec4(camPos, 1.0f)};

        cb.bindPipeline(m_Pipeline)
            .bindDescriptorSet(0, descriptorSet)
            .beginRendering({
                .area             = {.extent = extent2D},
                .depthAttachment  = rhi::AttachmentInfo {.target = &m_DepthTexture, .clearValue = 1.0f},
                .colorAttachments = {rhi::AttachmentInfo {.target     = &rtv.texture,
                                                          .clearValue = glm::vec4 {0.1f, 0.1f, 0.1f, 1.0f}}},
            })
            .pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pushConstants);

        for (const auto& sm : m_MeshResource->subMeshes)
        {
            cb.draw(rhi::GeometryInfo {
                .vertexBuffer = m_MeshResource->vertexBuffer.get(),
                .vertexOffset = sm.vertexOffset,
                .numVertices  = sm.vertexCount,
                .indexBuffer  = m_MeshResource->indexBuffer.get(),
                .indexOffset  = sm.indexOffset,
                .numIndices   = sm.indexCount,
            });
        }

        cb.endRendering();

        rhi::prepareForReading(cb, rtv.texture);
        rhi::prepareForReading(cb, m_DepthTexture);

        ImGuiApp::onRender(cb, rtv, dt);
    }

    void onResize(uint32_t width, uint32_t height) override
    {
        // Create a new depth texture
        rhi::Extent2D extent2D {width, height};
        m_DepthTexture = rhi::Texture::Builder {}
                             .setExtent(extent2D)
                             .setPixelFormat(rhi::PixelFormat::eDepth24_Stencil8)
                             .setNumMipLevels(1)
                             .setNumLayers(std::nullopt)
                             .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                            rhi::ImageUsage::eTransferSrc)
                             .build(*m_RenderDevice);
        ImGuiApp::onResize(width, height);
    }

private:
    Ref<gfx::MeshResource> m_MeshResource {nullptr};

    rhi::AccelerationStructure m_TLAS; // Required for ray queries
    rhi::GraphicsPipeline      m_Pipeline;

    rhi::Texture m_DepthTexture;
};

CONFIG_MAIN(RayQueryApp)