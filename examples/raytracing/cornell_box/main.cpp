#include <vultra/core/app/demo_app_entry.hpp>
#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/rhi/raytracing_pipeline.hpp>
#include <vultra/core/rhi/structs/raytracing_pipeline_properties.hpp>
#include <vultra/core/rhi/structs/render_device_structs.hpp>
#include <vultra/core/rhi/util.hpp>
#include <vultra/function/camera/camera_system.hpp>
#include <vultra/function/resource/gpu_scene_database.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include "../../example_renderer.hpp"

#include <imgui.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec3.hpp>

using namespace vultra;

namespace
{
    constexpr const char* kCornellBoxUri = "res://models/CornellBox/CornellBox-Original.obj";

    const char* const kRaygenCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_shader_image_load_formatted : enable

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0, rgba16f) uniform image2D image;

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
    vec2 uv = (vec2(gl_LaunchIDEXT.xy) + 0.5) / vec2(gl_LaunchSizeEXT.xy);
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 world = invViewProj * vec4(ndc, 0.0, 1.0);
    world /= world.w;

    hitValue = vec3(0.0);
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0,
                camPos, 0.001, normalize(world.xyz - camPos), 10000.0, 0);
    imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 1.0));
}
)";

    const char* const kMissCode = R"(
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

    const char* const kShadowMissCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 1) rayPayloadInEXT bool shadowed;

void main()
{
    shadowed = false;
}
)";

    const char* const kClosestHitCode = R"(
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

struct GpuMaterial {
    uint model;
    uint blockOffsetBytes;
    uint tableIndex;
    uint padding;
};
layout(std430, set = 0, binding = 2) readonly buffer Materials { GpuMaterial materials[]; };
layout(std430, set = 0, binding = 3) readonly buffer MaterialParams { vec4 materialParams[]; };

struct GPUInstanceData {
    uint geometryOffset;
    uint geometryCount;
    uint materialOffset;
    uint materialCount;
};
layout(std430, set = 0, binding = 4) readonly buffer InstanceData { GPUInstanceData instances[]; };

struct GPUGeometryNode {
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint vertexOffset;
    uint materialIndex;
    uint vertexStrideBytes;
    uint positionOffsetBytes;
    uint normalOffsetBytes;
    uint texCoord0OffsetBytes;
    uint tangentOffsetBytes;
};
layout(std430, set = 0, binding = 5) readonly buffer GeometryNodes { GPUGeometryNode geometryNodes[]; };

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer IndexBuffer { uint indices[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer FloatBuffer { float values[]; };

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

const uint VULTRA_MAT_PBRMR = 1u;
const uint VULTRA_MAT_UNLIT = 3u;
const uint VULTRA_MAT_PHONG = 4u;
const uint INVALID_OFFSET = 0xFFFFFFFFu;

vec3 loadVec3(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, vec3 fallback)
{
    if (offsetBytes == INVALID_OFFSET)
        return fallback;
    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return vec3(data.values[0], data.values[1], data.values[2]);
}

vec3 materialBaseColor(uint materialIndex)
{
    GpuMaterial m = materials[nonuniformEXT(materialIndex)];
    vec4 p0 = materialParams[m.blockOffsetBytes / 16u];
    return p0.rgb;
}

vec3 toneMappingKhronosPbrNeutral(vec3 color)
{
    const float startCompression = 0.76;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3(1.0), g);
}

vec3 linearTosRGB(vec3 color)
{
    bvec3 cutoff = lessThan(color, vec3(0.0031308));
    vec3 higher = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    vec3 lower = color * 12.92;
    return mix(higher, lower, cutoff);
}

void main()
{
    GPUInstanceData instance = instances[nonuniformEXT(gl_InstanceCustomIndexEXT)];
    GPUGeometryNode node = geometryNodes[nonuniformEXT(instance.geometryOffset + gl_GeometryIndexEXT)];

    IndexBuffer ib = IndexBuffer(node.indexBufferAddress);
    uint i0 = node.vertexOffset + ib.indices[gl_PrimitiveID * 3 + 0];
    uint i1 = node.vertexOffset + ib.indices[gl_PrimitiveID * 3 + 1];
    uint i2 = node.vertexOffset + ib.indices[gl_PrimitiveID * 3 + 2];

    vec3 p0 = loadVec3(node.vertexBufferAddress, i0, node.vertexStrideBytes, node.positionOffsetBytes, vec3(0.0));
    vec3 p1 = loadVec3(node.vertexBufferAddress, i1, node.vertexStrideBytes, node.positionOffsetBytes, vec3(0.0));
    vec3 p2 = loadVec3(node.vertexBufferAddress, i2, node.vertexStrideBytes, node.positionOffsetBytes, vec3(0.0));
    vec3 n0 = loadVec3(node.vertexBufferAddress, i0, node.vertexStrideBytes, node.normalOffsetBytes, vec3(0.0, 1.0, 0.0));
    vec3 n1 = loadVec3(node.vertexBufferAddress, i1, node.vertexStrideBytes, node.normalOffsetBytes, vec3(0.0, 1.0, 0.0));
    vec3 n2 = loadVec3(node.vertexBufferAddress, i2, node.vertexStrideBytes, node.normalOffsetBytes, vec3(0.0, 1.0, 0.0));

    vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 localPos = p0 * bary.x + p1 * bary.y + p2 * bary.z;
    vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(localPos, 1.0));
    vec3 localGeometricNormal = normalize(cross(p1 - p0, p2 - p0));
    vec3 normal = normalize(mat3(gl_ObjectToWorldEXT) * localGeometricNormal);
    normal = faceforward(normal, gl_WorldRayDirectionEXT, normal);

    uint materialIndex = instance.materialOffset + node.materialIndex;
    vec3 baseColor = materialBaseColor(materialIndex);

    vec3 l0 = lightVertices[0].xyz;
    vec3 l1 = lightVertices[1].xyz;
    vec3 l2 = lightVertices[2].xyz;
    vec3 l3 = lightVertices[3].xyz;
    vec3 samplePos = 0.25 * (l0 + l1 + l2 + l3);

    vec3 lightU = l1 - l0;
    vec3 lightV = l3 - l0;
    vec3 lightRel = worldPos - samplePos;
    float lightPlaneDistance = abs(dot(lightRel, normalize(cross(lightU, lightV))));
    float lightUCoord = dot(lightRel, lightU) / max(dot(lightU, lightU), 1e-4);
    float lightVCoord = dot(lightRel, lightV) / max(dot(lightV, lightV), 1e-4);
    bool hitLightSurface = lightPlaneDistance < 0.03 && abs(lightUCoord) <= 0.55 && abs(lightVCoord) <= 0.55;
    if (hitLightSurface)
    {
        hitValue = linearTosRGB(toneMappingKhronosPbrNeutral(lightColorIntensity.rgb * lightColorIntensity.a));
        return;
    }

    vec3 toLight = samplePos - worldPos;
    float dist = length(toLight);
    vec3 lightDir = normalize(toLight);
    vec3 origin = worldPos + normal * 1e-3;

    shadowed = true;
    traceRayEXT(topLevelAS,
                gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                0xff, 0, 0, 1,
                origin, 1e-4, lightDir, max(dist - 0.05, 1e-4), 1);

    vec3 directLighting = vec3(0.06);
    if (!shadowed)
    {
        vec3 lightNormal = normalize(cross(lightU, lightV));
        float nDotL = max(dot(normal, lightDir), 0.0);
        float cosThetaL = max(dot(lightNormal, -lightDir), 0.0);
        float area = length(cross(lightU, lightV));
        directLighting += lightColorIntensity.rgb * lightColorIntensity.a * nDotL * cosThetaL * area / max(dist * dist, 1e-4);
    }

    hitValue = linearTosRGB(toneMappingKhronosPbrNeutral(baseColor * directLighting));
}
)";

    void
    addNamedTransform(entt::registry& reg, entt::entity entity, const char* name, const TransformComponent& transform)
    {
        reg.emplace<NameComponent>(entity, NameComponent {name});
        reg.emplace<TransformComponent>(entity, transform);
    }
} // namespace

class CornellBoxRenderer final : public Renderer
{
public:
    std::string_view name() const override { return "cornell_box_rt"; }

    bool usesFrameGraph() const override { return false; }
    bool requiresRayTracingScene() const override { return true; }

    void render(ImmediateRenderContext& ctx) override
    {
        auto* db     = ctx.view().gpuSceneDatabase;
        auto* target = ctx.view().target;
        if (!db || !target || !db->rayTracingTlas || !db->rayTracingGeometryNodeBuffer ||
            !db->rayTracingInstanceBuffer || !db->resources || !db->resources->materialParams.gpu ||
            !db->resources->materialTableBuffer)
            return;

        ensurePipeline(ctx.rd);
        ensureOutput(ctx.rd, target->getExtent());
        if (!m_Pipeline || !m_OutputImage)
            return;

        rhi::prepareForRaytracing(ctx.cb, m_OutputImage);
        auto descriptorSet = ctx.cb.createDescriptorSetBuilder()
                                 .bind(0, rhi::bindings::AccelerationStructureKHR {.as = &db->rayTracingTlas})
                                 .bind(1,
                                       rhi::bindings::StorageImage {
                                           .texture     = &m_OutputImage,
                                           .imageAspect = rhi::ImageAspect::eColor,
                                       })
                                 .bind(2,
                                       rhi::bindings::StorageBuffer {
                                           .buffer = db->resources->materialTableBuffer.get(),
                                       })
                                 .bind(3,
                                       rhi::bindings::StorageBuffer {
                                           .buffer = db->resources->materialParams.gpu.get(),
                                       })
                                 .bind(4,
                                       rhi::bindings::StorageBuffer {
                                           .buffer = db->rayTracingInstanceBuffer.get(),
                                       })
                                 .bind(5,
                                       rhi::bindings::StorageBuffer {
                                           .buffer = db->rayTracingGeometryNodeBuffer.get(),
                                       })
                                 .build(m_Pipeline.getDescriptorSetLayout(0));

        const auto* camera = ctx.view().camera;
        if (!camera)
            return;

        struct GlobalPushConstants
        {
            glm::mat4 invViewProj;
            glm::vec3 camPos;
            float     padding;
            glm::vec4 missColor;
            glm::vec4 lightColorIntensity;
            glm::vec4 lightVertices[4];
        } pc {};

        auto projection = camera->projection;
        if (ctx.rd.getBackendApi() == rhi::RenderBackendApi::eVulkan)
            projection[1][1] *= -1.0f;
        pc.invViewProj = glm::inverse(projection * camera->view);
        pc.camPos      = glm::vec3(camera->inverseView[3]);
        pc.missColor   = {0.2f, 0.3f, 0.3f, 1.0f};
        fillLightConstants(pc.lightColorIntensity, pc.lightVertices);

        ctx.cb.bindPipeline(m_Pipeline)
            .bindDescriptorSet(0, descriptorSet)
            .pushConstants(
                rhi::ShaderStages::eRayGen | rhi::ShaderStages::eMiss | rhi::ShaderStages::eClosestHit, 0, &pc)
            .traceRays(*m_Pipeline.getSBT(ctx.rd), {target->getExtent().width, target->getExtent().height, 1u});

        rhi::prepareForReading(ctx.cb, m_OutputImage);
        rhi::prepareForAttachment(ctx.cb, *target, false);
        ctx.cb.blit(m_OutputImage, *target, rhi::TexelFilter::eLinear);
    }

    void onImGui() override
    {
        if (auto* services = getServices())
            examples::suppressCameraWhenUsingImGui(*services);

        ImGui::Begin("Raytracing Cornell Box", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted("This is a simple raytracing example rendering the Cornell Box scene.");
        examples::drawNamedLightControls(*getServices(), "Ceiling Light");
        examples::drawRenderDocCaptureButton(*getServices());
        ImGui::End();
    }

    void onResize(uint32_t width, uint32_t height) override
    {
        m_OutputImage  = {};
        m_OutputExtent = {width, height};
    }

private:
    void ensurePipeline(rhi::RenderDevice& rd)
    {
        if (m_Pipeline)
            return;

        m_Pipeline = rhi::RayTracingPipeline::Builder {}
                         .setMaxRecursionDepth(2)
                         .addShader(rhi::ShaderType::eRayGen, {.code = kRaygenCode})
                         .addShader(rhi::ShaderType::eMiss, {.code = kMissCode})
                         .addShader(rhi::ShaderType::eMiss, {.code = kShadowMissCode})
                         .addShader(rhi::ShaderType::eClosestHit, {.code = kClosestHitCode})
                         .addRaygenGroup(0)
                         .addMissGroup(1)
                         .addMissGroup(2)
                         .addHitGroup(3)
                         .build(rd);
    }

    void ensureOutput(rhi::RenderDevice& rd, rhi::Extent2D extent)
    {
        if (m_OutputImage && m_OutputImage.getExtent() == extent)
            return;

        m_OutputExtent = extent;
        m_OutputImage  = rhi::Texture::Builder {}
                            .setExtent(extent)
                            .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                            .setNumMipLevels(1)
                            .setNumLayers(std::nullopt)
                            .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eTransferSrc)
                            .setupOptimalSampler(false)
                            .build(rd);
    }

    void fillLightConstants(glm::vec4& colorIntensity, glm::vec4 (&vertices)[4])
    {
        glm::vec3 pos {-0.005f, 1.98f, -0.03f};
        glm::vec3 color {1.0f, 0.95f, 0.85f};
        float     intensity = 12.0f;
        float     width     = 0.47f;
        float     height    = 0.38f;

        if (auto* services = getServices())
        {
            if (auto* worldService = services->tryGet<IWorldService>())
            {
                auto& reg  = worldService->world().registry();
                auto  view = reg.view<NameComponent, TransformComponent, LightComponent>();
                for (auto entity : view)
                {
                    auto& name = view.get<NameComponent>(entity);
                    if (name.name != "Ceiling Light")
                        continue;
                    auto& transform = view.get<TransformComponent>(entity);
                    auto& light     = view.get<LightComponent>(entity);
                    pos             = transform.position;
                    color           = light.color;
                    intensity       = light.intensity;
                    width           = light.width;
                    height          = light.height;
                    break;
                }
            }
        }

        colorIntensity = glm::vec4(color, intensity);
        const glm::vec3 u {width * 0.5f, 0.0f, 0.0f};
        const glm::vec3 v {0.0f, 0.0f, height * 0.5f};
        vertices[0] = glm::vec4(pos - u - v, 1.0f);
        vertices[1] = glm::vec4(pos + u - v, 1.0f);
        vertices[2] = glm::vec4(pos + u + v, 1.0f);
        vertices[3] = glm::vec4(pos - u + v, 1.0f);
    }

private:
    rhi::RayTracingPipeline m_Pipeline;
    rhi::Texture            m_OutputImage;
    rhi::Extent2D           m_OutputExtent {};
};

class RaytracingCornellBoxApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "Raytracing Cornell Box"; }

    rhi::RenderDeviceFeatureFlagBits demoRenderDeviceFeatureFlag() const override
    {
        return rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline;
    }

    Ref<Renderer> makeRenderer() const override { return createRef<CornellBoxRenderer>(); }

    FPSCameraController makeFPSCameraController() const override
    {
        auto controller          = DemoAppHost::makeFPSCameraController();
        controller.position      = {0.0f, 1.0f, 4.0f};
        controller.yawDegrees    = -90.0f;
        controller.pitchDegrees  = 0.0f;
        controller.orbitDistance = 4.0f;
        controller.zFar          = 100.0f;
        return controller;
    }

    void onPostConfigureDemo(Engine& engine) override
    {
        auto& renderService = engine.ctx().services.require<IRenderService>();
        auto& sceneService  = engine.ctx().services.require<ISceneService>();
        auto& world         = engine.ctx().services.require<IWorldService>().world();
        auto& reg           = world.registry();

        auto box = sceneService.instantiateScene(world, kCornellBoxUri);
        if (box == entt::null)
        {
            VULTRA_CLIENT_ERROR("[RaytracingCornellBox] Failed to instantiate scene: {}", kCornellBoxUri);
            return;
        }
        if (auto* name = reg.try_get<NameComponent>(box))
            name->name = "Cornell Box";

        auto light = world.createEntity();
        addNamedTransform(reg,
                          light,
                          "Ceiling Light",
                          TransformComponent {
                              .position = {-0.005f, 1.98f, -0.03f},
                              .rotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f},
                              .scale    = {1.0f, 1.0f, 1.0f},
                          });
        reg.emplace<LightComponent>(light,
                                    LightComponent {
                                        .kind      = 3u,
                                        .color     = {1.0f, 0.95f, 0.85f},
                                        .intensity = 12.0f,
                                        .range     = 10.0f,
                                        .width     = 0.47f,
                                        .height    = 0.38f,
                                    });

        auto& settings                        = renderService.builtinRenderSettings();
        settings.pbrLighting.enableIBL        = false;
        settings.pbrLighting.ambientIntensity = 0.15f;
    }
};

VULTRA_DEMO_APP_MAIN(RaytracingCornellBoxApp)
