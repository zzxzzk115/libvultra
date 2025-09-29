#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input.hpp>
#include <vultra/core/rhi/raytracing/raytracing_pipeline.hpp>
#include <vultra/function/app/imgui_app.hpp>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace vultra;

struct SimpleVertex
{
    glm::vec3 position;
};

// Triangle in NDC for simplicity.
constexpr auto kTriangle = std::array {
    // clang-format off
    //                    position
    SimpleVertex{ {  0.0f, -0.5f, 0.0f } }, // top
    SimpleVertex{ { -0.5f,  0.5f, 0.0f } }, // left
    SimpleVertex{ {  0.5f,  0.5f, 0.0f } }, // right
    // clang-format on
};

constexpr auto kIndices = std::array {
    0u,
    1u,
    2u,
};

constexpr glm::mat4 kTransform = glm::mat4(1.0f);

const char* const raygenCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_shader_image_load_formatted : enable

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0) uniform image2D image;

layout(location = 0) rayPayloadEXT vec3 hitValue;

void main()
{
    vec3 origin = vec3(0.0, 0.0, -2.0);
    vec2 uv = (vec2(gl_LaunchIDEXT.xy) + 0.5) / vec2(gl_LaunchSizeEXT.xy);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 target = vec3(ndc, 0.0);
    vec3 direction = normalize(target - origin);
    hitValue = vec3(0.0);
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, origin, 0.001, direction, 10000.0, 0);
    imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 0.0));
}
)";

const char* const missCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

layout(push_constant) uniform PushConstants
{
    vec4 missColor;
};

void main()
{
    hitValue = missColor.rgb;
}
)";

const char* const closestHitCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

void main()
{
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    hitValue = barycentricCoords;
}
)";

class RaytracingTriangleApp final : public ImGuiApp
{
public:
    explicit RaytracingTriangleApp(const std::span<char*>& args) :
        ImGuiApp(args,
                 {.title                   = "Raytracing Triangle Example",
                  .renderDeviceFeatureFlag = rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline,
                  .vSyncConfig             = rhi::VerticalSync::eEnabled},
                 {.enableDocking = false})
    {
        // Create vertex buffer
        m_VertexBuffer =
            std::move(m_RenderDevice->createVertexBuffer(sizeof(SimpleVertex), 3, rhi::AllocationHints::eNone));

        // Upload vertex buffer
        {
            constexpr auto kVerticesSize       = sizeof(SimpleVertex) * kTriangle.size();
            auto           stagingVertexBuffer = m_RenderDevice->createStagingBuffer(kVerticesSize, kTriangle.data());

            m_RenderDevice->execute(
                [&](auto& cb) {
                    cb.copyBuffer(stagingVertexBuffer, m_VertexBuffer, vk::BufferCopy {0, 0, kVerticesSize});
                },
                true);
        }
        const auto vertexBufferAddress = m_RenderDevice->getBufferDeviceAddress(m_VertexBuffer);

        // Create index buffer
        {
            m_IndexBuffer = std::move(
                m_RenderDevice->createIndexBuffer(rhi::IndexType::eUInt32, 3, rhi::AllocationHints::eNone));

            // Upload index buffer
            constexpr auto kIndicesSize       = sizeof(uint32_t) * kIndices.size();
            auto           stagingIndexBuffer = m_RenderDevice->createStagingBuffer(kIndicesSize, kIndices.data());

            m_RenderDevice->execute(
                [&](auto& cb) {
                    cb.copyBuffer(stagingIndexBuffer, m_IndexBuffer, vk::BufferCopy {0, 0, kIndicesSize});
                },
                true);
        }
        const auto indexBufferAddress = m_RenderDevice->getBufferDeviceAddress(m_IndexBuffer);

        // Create transform buffer
        {
            m_TransformBuffer = std::move(m_RenderDevice->createTransformBuffer());

            // Convert to row-major
            glm::mat4 rowMajor = glm::transpose(kTransform);

            // Host visible & coherent memory, so we can directly map and copy
            void* mapped = m_TransformBuffer.map();
            memcpy(mapped, &rowMajor, sizeof(vk::TransformMatrixKHR));
            m_TransformBuffer.unmap();
        }
        const auto transformBufferAddress = m_RenderDevice->getBufferDeviceAddress(m_TransformBuffer);

        // Create and build BLAS
        m_BLAS = m_RenderDevice->createBuildSingleGeometryBLAS(vertexBufferAddress,
                                                               indexBufferAddress,
                                                               transformBufferAddress,
                                                               sizeof(SimpleVertex),
                                                               kTriangle.size(),
                                                               kIndices.size());

        // Create and build TLAS
        m_TLAS = m_RenderDevice->createBuildTLAS(m_BLAS, kTransform);

        // Create raytracing pipeline
        m_Pipeline = rhi::RayTracingPipeline::Builder {}
                         .setMaxRecursionDepth(1)
                         .addShader(rhi::ShaderType::eRayGen, {.code = raygenCode})
                         .addShader(rhi::ShaderType::eMiss, {.code = missCode})
                         .addShader(rhi::ShaderType::eClosestHit, {.code = closestHitCode})
                         .addRaygenGroup(0)
                         .addMissGroup(1)
                         .addHitGroup(2)
                         .build(*m_RenderDevice);

        // Create SBT
        m_SBT = m_RenderDevice->createShaderBindingTable(m_Pipeline);

        // Create output image
        rhi::Extent2D extent {static_cast<uint32_t>(m_Window.getExtent().x),
                              static_cast<uint32_t>(m_Window.getExtent().y)};
        m_OutputImage = rhi::Texture::Builder {}
                            .setExtent(extent)
                            .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                            .setNumMipLevels(1)
                            .setNumLayers(std::nullopt)
                            .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eTransferSrc)
                            .setupOptimalSampler(false)
                            .build(*m_RenderDevice);
    }

    void onImGui() override
    {
        ImGui::Begin("Raytracing Triangle Example");
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
        // Record raytracing commands
        rhi::prepareForRaytracing(cb, m_OutputImage);

        auto descriptorSet =
            cb.createDescriptorSetBuilder()
                .bind(0, rhi::bindings::AccelerationStructureKHR {.as = &m_TLAS})
                .bind(1,
                      rhi::bindings::StorageImage {.texture = &m_OutputImage, .imageAspect = rhi::ImageAspect::eColor})
                .build(m_Pipeline.getDescriptorSetLayout(0));

        const glm::vec4 missColor = glm::vec4(0.2f, 0.3f, 0.3f, 1.0f);

        cb.bindPipeline(m_Pipeline)
            .bindDescriptorSet(0, descriptorSet)
            .pushConstants(rhi::ShaderStages::eMiss, 0, &missColor)
            .traceRays(m_SBT, {m_Window.getExtent(), 1});

        cb.blit(m_OutputImage, rtv.texture, vk::Filter::eLinear);

        ImGuiApp::onRender(cb, rtv, dt);
    }

    void onResize(uint32_t width, uint32_t height) override
    {
        // Recreate output image on resize
        m_OutputImage = rhi::Texture::Builder {}
                            .setExtent({width, height})
                            .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                            .setNumMipLevels(1)
                            .setNumLayers(std::nullopt)
                            .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eTransferSrc)
                            .setupOptimalSampler(false)
                            .build(*m_RenderDevice);

        ImGuiApp::onResize(width, height);
    }

private:
    rhi::VertexBuffer          m_VertexBuffer;
    rhi::IndexBuffer           m_IndexBuffer;
    rhi::Buffer                m_TransformBuffer;
    rhi::AccelerationStructure m_BLAS, m_TLAS;
    rhi::RayTracingPipeline    m_Pipeline;
    rhi::ShaderBindingTable    m_SBT;
    rhi::Texture               m_OutputImage;
};

CONFIG_MAIN(RaytracingTriangleApp)