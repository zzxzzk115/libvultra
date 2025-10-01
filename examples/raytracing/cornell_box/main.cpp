#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input.hpp>
#include <vultra/core/rhi/raytracing/raytracing_pipeline.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/renderer/mesh_manager.hpp>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace vultra;

constexpr glm::mat4 kTransform = glm::mat4(1.0f);

const char* const raygenCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_shader_image_load_formatted : enable

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0) uniform image2D image;

layout(location = 0) rayPayloadEXT vec3 hitValue;

layout(push_constant) uniform GlobalPushConstants
{
    mat4 invViewProj;
    vec3 camPos;
    float _pad;
    vec4 missColor;
    vec4 lightColorIntensity;
    vec4 lightVertices[4];
};

void main()
{
    vec2 uv  = (vec2(gl_LaunchIDEXT.xy) + 0.5) / vec2(gl_LaunchSizeEXT.xy);
    vec2 ndc = uv * 2.0 - 1.0;

    vec4 clip  = vec4(ndc, 0.0, 1.0);
    vec4 world = invViewProj * clip;
    world /= world.w;

    vec3 origin    = camPos;
    vec3 direction = normalize(world.xyz - camPos);

    hitValue = vec3(0.0);
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0,
                origin, 0.001, direction, 10000.0, 0);

    imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 0.0));
}
)";

const char* const missCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

layout(push_constant) uniform GlobalPushConstants
{
    mat4 invViewProj;
    vec3 camPos;
    float _pad;
    vec4 missColor;
    vec4 lightColorIntensity;
    vec4 lightVertices[4];
};

void main()
{
    hitValue = missColor.rgb;
}
)";

const char* const shadowMissCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 1) rayPayloadInEXT bool shadowed;

void main()
{
    shadowed = false;
}
)";

const char* const closestHitCode = R"(
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord;
    vec4 tangent;
};

layout(buffer_reference, scalar) buffer VertexBuffer { Vertex v[]; };
layout(buffer_reference, scalar) buffer IndexBuffer  { uint i[]; };

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

struct GPUMaterial {
    vec4 baseColor;
    vec4 ambientColor;
    vec4 emissiveIntensity;
};
layout(binding = 2, set = 0) buffer Materials { GPUMaterial materials[]; };

struct GPUGeometryNode {
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint materialIndex;
};
layout(binding = 3, set = 0) buffer GeometryNodes { GPUGeometryNode geometryNodes[]; };

layout(push_constant) uniform GlobalPushConstants
{
    mat4 invViewProj;
    vec3 camPos;
    float _pad;
    vec4 missColor;
    vec4 lightColorIntensity;
    vec4 lightVertices[4];
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT bool shadowed;
hitAttributeEXT vec2 attribs;

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

const float kGamma = 2.4;
const float kInvGamma = 1.0 / kGamma;

vec3 linearTosRGB(vec3 color) {
    #if 1
  const bvec3 cutoff = lessThan(color, vec3(0.0031308));
    const vec3 higher = 1.055 * pow(color, vec3(kInvGamma)) - 0.055;
    const vec3 lower = color * 12.92;
    return mix(higher, lower, cutoff);
    #else
  return pow(color, vec3(kInvGamma));
    #endif
}

vec3 toneMappingKhronosPbrNeutral(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    const float d = 1. - startCompression;
    float newPeak = 1. - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
    return mix(color, newPeak * vec3(1, 1, 1), g);
}

void main()
{
    const uint geomIndex = gl_GeometryIndexEXT;
    GPUGeometryNode node = geometryNodes[nonuniformEXT(geomIndex)];
    GPUMaterial mat = materials[nonuniformEXT(node.materialIndex)];

    vec3 baseColor = mat.baseColor.rgb;
    vec3 emissiveColor = mat.emissiveIntensity.rgb * mat.emissiveIntensity.a;
    vec3 lightColor = lightColorIntensity.rgb * lightColorIntensity.a;

    // Light surface hit -> return emissive
    if (length(emissiveColor) > 0.0) {
        hitValue = emissiveColor;
        return;
    }

    VertexBuffer vb = VertexBuffer(node.vertexBufferAddress);
    IndexBuffer ib  = IndexBuffer(node.indexBufferAddress);

    const uint i0 = ib.i[gl_PrimitiveID * 3 + 0];
    const uint i1 = ib.i[gl_PrimitiveID * 3 + 1];
    const uint i2 = ib.i[gl_PrimitiveID * 3 + 2];

    Vertex v0 = vb.v[i0];
    Vertex v1 = vb.v[i1];
    Vertex v2 = vb.v[i2];

    vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 worldPos = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
    vec3 normal   = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);

    // --- sample point on area light
    vec3 p0 = lightVertices[0].xyz;
    vec3 p1 = lightVertices[1].xyz;
    vec3 p2 = lightVertices[2].xyz;
    vec3 p3 = lightVertices[3].xyz;

    float r1 = rand(vec2(gl_LaunchIDEXT.xy) + float(gl_PrimitiveID));
    float r2 = rand(vec2(gl_LaunchIDEXT.yx) + float(gl_PrimitiveID) * 0.37);
    // vec3 samplePos = mix(mix(p0, p1, r1), mix(p3, p2, r1), r2);

    // NOTE: Fallback to point light here.
    // TODO: Monte Carlo sampling & TAA
    vec3 samplePos = 0.25 * (p0 + p1 + p2 + p3);

    vec3 toLight = samplePos - worldPos;
    float dist   = length(toLight);
    vec3 lightDir = normalize(toLight);

    // shadow test
    float tmin = 1e-5;
    float tmax = dist - 1e-3;
	vec3 origin = worldPos + normal * 1e-5;
    shadowed = true;
    traceRayEXT(topLevelAS,
                gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                0xff,
                0,
                0,
                1, // missGroupIndex = 1
                origin,
                tmin,
                lightDir, 
                tmax,
                1); // shadow payload location = 1

    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 directLighting = vec3(0.0);

    if (!shadowed) {
        float area = length(cross(p1 - p0, p3 - p0));
        vec3 lightNormal = normalize(cross(p1 - p0, p3 - p0));
        float cosThetaL = max(dot(lightNormal, -lightDir), 0.0);
        directLighting = lightColor * NdotL * cosThetaL * area / (dist * dist);
    }

    hitValue = linearTosRGB(toneMappingKhronosPbrNeutral(baseColor * directLighting));
}
)";

// Only base color for simplicity
struct GPUMaterial
{
    glm::vec4 baseColor {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 ambientColor {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 emissiveIntensity {0.0f, 0.0f, 0.0f, 1.0f};
};

struct GPUGeometryNode
{
    uint64_t vertexBufferAddress {0};
    uint64_t indexBufferAddress {0};
    uint32_t materialIndex {0};
};

class RaytracingCornellBoxApp final : public ImGuiApp
{
public:
    explicit RaytracingCornellBoxApp(const std::span<char*>& args) :
        ImGuiApp(args,
                 {.title                   = "Raytracing Cornell Box Example",
                  .renderDeviceFeatureFlag = rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline,
                  .vSyncConfig             = rhi::VerticalSync::eEnabled},
                 {.enableDocking = false})
    {
        // Load cornell box model
        m_MeshResource =
            resource::loadResource<gfx::MeshManager>("resources/models/CornellBox/CornellBox-Original.obj");

        assert(m_MeshResource->lights.size() > 0);
        assert(m_MeshResource->lights[0].vertices.size() >= 4);
        for (size_t i = 0; i < 4; ++i)
        {
            m_LightVertices[i] = glm::vec4(m_MeshResource->lights[0].vertices[i].position, 1.0f);
        }

        // Create and build TLAS
        m_TLAS = m_RenderDevice->createBuildTLAS(m_MeshResource->renderMesh.blas, kTransform);

        // Create raytracing pipeline
        m_Pipeline = rhi::RayTracingPipeline::Builder {}
                         .setMaxRecursionDepth(2)                                           // primary + shadow
                         .addShader(rhi::ShaderType::eRayGen, {.code = raygenCode})         // raygenGroup[0]
                         .addShader(rhi::ShaderType::eMiss, {.code = missCode})             // missGroup[0] primary
                         .addShader(rhi::ShaderType::eMiss, {.code = shadowMissCode})       // missGroup[1] shadow
                         .addShader(rhi::ShaderType::eClosestHit, {.code = closestHitCode}) // hitGroup[0] primary hit
                         .addRaygenGroup(0)
                         .addMissGroup(1) // miss group 0
                         .addMissGroup(2) // miss group 1 (shadow)
                         .addHitGroup(3)  // only primary hit group
                         .build(*m_RenderDevice);

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

        // Create material buffer
        {
            std::vector<GPUMaterial> materials;
            materials.reserve(m_MeshResource->materials.size());

            for (const auto& mat : m_MeshResource->materials)
            {
                GPUMaterial gpuMat {};
                gpuMat.baseColor         = glm::vec4(mat.baseColor.r, mat.baseColor.g, mat.baseColor.b, 1.0f);
                gpuMat.ambientColor      = mat.ambientColor;
                gpuMat.emissiveIntensity = mat.emissiveColorIntensity;
                materials.push_back(gpuMat);
            }

            m_MaterialBuffer = m_RenderDevice->createStorageBuffer(
                sizeof(GPUMaterial) * static_cast<vk::DeviceSize>(materials.size()), rhi::AllocationHints::eNone);

            auto stagingBuffer = m_RenderDevice->createStagingBuffer(
                sizeof(GPUMaterial) * static_cast<vk::DeviceSize>(materials.size()), materials.data());

            m_RenderDevice->execute(
                [&](rhi::CommandBuffer& cb) {
                    cb.copyBuffer(stagingBuffer, m_MaterialBuffer, vk::BufferCopy {0, 0, stagingBuffer.getSize()});
                },
                true);
        }

        // Create geometry node buffer
        {
            std::vector<GPUGeometryNode> geometryNodes;
            geometryNodes.reserve(m_MeshResource->renderMesh.subMeshes.size());

            for (const auto& sm : m_MeshResource->renderMesh.subMeshes)
            {
                GPUGeometryNode node {};
                node.materialIndex       = sm.materialIndex;
                node.vertexBufferAddress = sm.vertexBufferAddress;
                node.indexBufferAddress  = sm.indexBufferAddress;
                geometryNodes.push_back(node);
            }

            m_GeometryNodeBuffer = m_RenderDevice->createStorageBuffer(
                sizeof(GPUGeometryNode) * static_cast<vk::DeviceSize>(geometryNodes.size()),
                rhi::AllocationHints::eNone);

            auto stagingBuffer = m_RenderDevice->createStagingBuffer(
                sizeof(GPUGeometryNode) * static_cast<vk::DeviceSize>(geometryNodes.size()), geometryNodes.data());

            m_RenderDevice->execute(
                [&](rhi::CommandBuffer& cb) {
                    cb.copyBuffer(stagingBuffer, m_GeometryNodeBuffer, vk::BufferCopy {0, 0, stagingBuffer.getSize()});
                },
                true);
        }
    }

    void onImGui() override
    {
        ImGui::Begin("Raytracing Cornell Box Example");
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
        if (rtv.texture.getExtent() != m_OutputImage.getExtent())
        {
            VULTRA_CLIENT_TRACE("RTV size ({}, {}) != Output Image size ({}, {}), skipping rendering this frame",
                                rtv.texture.getExtent().width,
                                rtv.texture.getExtent().height,
                                m_OutputImage.getExtent().width,
                                m_OutputImage.getExtent().height);
            ImGuiApp::onRender(cb, rtv, dt);
            return;
        }

        // Record raytracing commands
        rhi::prepareForRaytracing(cb, m_OutputImage);

        auto descriptorSet =
            cb.createDescriptorSetBuilder()
                .bind(0, rhi::bindings::AccelerationStructureKHR {.as = &m_TLAS})
                .bind(1,
                      rhi::bindings::StorageImage {.texture = &m_OutputImage, .imageAspect = rhi::ImageAspect::eColor})
                .bind(2, rhi::bindings::StorageBuffer {.buffer = &m_MaterialBuffer})
                .bind(3, rhi::bindings::StorageBuffer {.buffer = &m_GeometryNodeBuffer})
                .build(m_Pipeline.getDescriptorSetLayout(0));

        glm::vec3 camPos = glm::vec3(0.0f, 1.0f, 4.0f);
        glm::mat4 projection =
            glm::perspective(glm::radians(45.0f),
                             static_cast<float>(m_Window.getExtent().x) / static_cast<float>(m_Window.getExtent().y),
                             0.1f,
                             100.0f);
        projection[1][1] *= -1; // Flip Y for Vulkan
        glm::mat4 view        = glm::lookAt(camPos, glm::vec3(camPos.x, camPos.y, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 viewProj    = projection * view;
        glm::mat4 invViewProj = glm::inverse(viewProj);

        const glm::vec4 missColor = glm::vec4(0.2f, 0.3f, 0.3f, 1.0f);

        struct GlobalPushConstants
        {
            glm::mat4 invViewProj; // for raygen
            glm::vec3 camPos;      // for raygen
            float     padding;
            glm::vec4 missColor;           // for miss
            glm::vec4 lightColorIntensity; // for area light
            glm::vec4 lightVertices[4];    // for area light
        };

        GlobalPushConstants pushConstants {
            invViewProj,
            camPos,
            0.0f,
            missColor,
        };

        pushConstants.lightColorIntensity = m_MeshResource->lights[0].colorIntensity;
        for (size_t i = 0; i < 4; ++i)
        {
            pushConstants.lightVertices[i] = m_LightVertices[i];
        }

        cb.bindPipeline(m_Pipeline)
            .bindDescriptorSet(0, descriptorSet)
            .pushConstants(rhi::ShaderStages::eRayGen | rhi::ShaderStages::eMiss | rhi::ShaderStages::eClosestHit,
                           0,
                           &pushConstants)
            .traceRays(*m_Pipeline.getSBT(*m_RenderDevice), {m_Window.getExtent(), 1});

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
    Ref<gfx::MeshResource> m_MeshResource {nullptr};

    rhi::Buffer                m_TransformBuffer;
    rhi::AccelerationStructure m_TLAS;
    rhi::RayTracingPipeline    m_Pipeline;
    rhi::Texture               m_OutputImage;

    rhi::Buffer m_MaterialBuffer;
    rhi::Buffer m_GeometryNodeBuffer;

    glm::vec4 m_LightVertices[4];
};

CONFIG_MAIN(RaytracingCornellBoxApp)