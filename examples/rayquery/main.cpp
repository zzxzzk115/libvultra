#include <vultra/core/app/demo_app_entry.hpp>
#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/render_device.hpp>
#include <vultra/function/asset/asset_handle.hpp>
#include <vultra/function/camera/camera_system.hpp>
#include <vultra/function/rendering/srp/renderer.hpp>
#include <vultra/function/resource/gpu_mesh.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/gpu_resource_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include "../example_renderer.hpp"

#include <imgui.h>
#include <vasset/vmesh.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <limits>
#include <vector>

using namespace vultra;

namespace
{
    constexpr const char* kSceneUri = "res://models/raytracing_shadow/raytracing_shadow.gltf";

    const char* const vertexCode = R"(
#version 460 core

layout (location = 0) in vec3 a_Position;
layout (location = 1) in vec3 a_Normal;

layout (location = 0) out vec3 v_Color;
layout (location = 1) out vec3 v_Normal;
layout (location = 2) out vec3 v_FragPos;

layout (push_constant) uniform GlobalPushConstants
{
    mat4 model;
    mat4 viewProjection;
    vec4 lightPos;
    vec4 cameraPos;
    vec4 lightColor;
    vec4 baseColor;
    vec4 shadingParams;
};

void main()
{
    v_Color = vec3(0.78, 0.72, 0.64);
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
    vec4 lightPos;
    vec4 cameraPos;
    vec4 lightColor;
    vec4 baseColor;
    vec4 shadingParams;
};

void main()
{
    vec3 N = normalize(v_Normal);
    vec3 L = normalize(lightPos.xyz - v_FragPos);
    vec3 V = normalize(cameraPos.xyz - v_FragPos);
    vec3 H = normalize(L + V);

    float diffuse = max(dot(N, L), shadingParams.x);
    float specular = pow(max(dot(N, H), 0.0), shadingParams.z) * shadingParams.y;
    vec3 color = baseColor.rgb * lightColor.rgb * diffuse + vec3(specular);

    float dist = length(lightPos.xyz - v_FragPos);
    vec3 rayOrigin = v_FragPos + N * 1e-4;

    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery, topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF,
                          rayOrigin, 1e-4, L, dist - 1e-3);

    while (rayQueryProceedEXT(rayQuery)) {
        if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            rayQueryConfirmIntersectionEXT(rayQuery);
            break;
        }
    }

    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        color *= shadingParams.w;
    }

    FragColor = vec4(color, 1.0);
}
)";
} // namespace

class RayQueryRenderer final : public Renderer
{
public:
    std::string_view name() const override { return "rayquery"; }

    [[nodiscard]] bool usesFrameGraph() const override { return false; }

    void init() override
    {
        // Scene meshes are instantiated by the app after renderer registration, so the ray-query resources are built
        // lazily on the first frame.
    }

    void render(ImmediateRenderContext& ctx) override
    {
        auto* target = ctx.view().target;
        auto* camera = ctx.view().camera;
        if (!target || !camera)
            return;
        if (!m_SceneResourcesReady)
            rebuildSceneResources(ctx.rd);
        if (!m_TLAS || m_DrawMeshes.empty())
            return;

        if (!m_DepthTexture || m_DepthTexture.getExtent() != target->getExtent())
        {
            m_DepthTexture = rhi::Texture::Builder {}
                                 .setExtent(target->getExtent())
                                 .setPixelFormat(rhi::PixelFormat::eDepth24_Stencil8)
                                 .setNumMipLevels(1)
                                 .setNumLayers(std::nullopt)
                                 .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                                rhi::ImageUsage::eTransferSrc)
                                 .build(ctx.rd);
        }

        auto& cb = ctx.cb;
        rhi::prepareForAttachment(cb, *target, false);
        rhi::prepareForAttachment(cb, m_DepthTexture, false);

        struct GlobalPushConstants
        {
            glm::mat4 model;
            glm::mat4 viewProjection;
            glm::vec4 lightPos;
            glm::vec4 cameraPos;
            glm::vec4 lightColor;
            glm::vec4 baseColor;
            glm::vec4 shadingParams;
        };

        auto projection = camera->projection;
        if (ctx.rd.getBackendApi() == rhi::RenderBackendApi::eVulkan)
            projection[1][1] *= -1.0f;

        GlobalPushConstants pushConstants {
            .model          = glm::mat4 {1.0f},
            .viewProjection = projection * camera->view,
            .lightPos       = glm::vec4 {m_LightPos, 1.0f},
            .cameraPos      = glm::vec4 {glm::vec3(camera->inverseView[3]), 1.0f},
            .lightColor     = glm::vec4 {m_LightColor, 1.0f},
            .baseColor      = glm::vec4 {m_BaseColor, 1.0f},
            .shadingParams  = glm::vec4 {m_AmbientFloor, m_SpecularStrength, m_SpecularPower, m_ShadowFactor},
        };

        RHI_GPU_ZONE(cb, "RayQuery");
        cb.beginRendering({
            .area             = {.extent = target->getExtent()},
            .colorAttachments = {rhi::AttachmentInfo {.target     = target,
                                                      .clearValue = glm::vec4 {0.08f, 0.09f, 0.11f, 1.0f}}},
            .depthAttachment  = rhi::AttachmentInfo {.target = &m_DepthTexture, .clearValue = 1.0f},
        });

        auto& gpuResources = getServices()->require<IGpuResourceService>();
        auto& pool         = gpuResources.pool();
        for (const auto& drawMesh : m_DrawMeshes)
        {
            if (drawMesh.meshIndex >= pool.meshes.size())
                continue;
            auto& mesh = pool.meshes[drawMesh.meshIndex];

            auto* pipeline = pipelineForMesh(ctx.rd, mesh);
            if (!pipeline)
                continue;

            auto descriptorSet = cb.createDescriptorSetBuilder()
                                     .bind(0, rhi::bindings::AccelerationStructureKHR {.as = &m_TLAS})
                                     .build(pipeline->getDescriptorSetLayout(0));

            pushConstants.model = drawMesh.transform;
            cb.bindPipeline(*pipeline)
                .bindDescriptorSet(0, descriptorSet)
                .pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pushConstants);

            for (const auto& subMesh : mesh.subMeshes)
            {
                cb.draw(rhi::GeometryInfo {
                    .vertexBuffer = &mesh.vertexBuffer,
                    .vertexOffset = subMesh.vertexOffset,
                    .numVertices  = subMesh.vertexCount,
                    .indexBuffer  = &mesh.indexBuffer,
                    .indexOffset  = subMesh.indexOffset,
                    .numIndices   = subMesh.indexCount,
                });
            }
        }

        cb.endRendering();

        rhi::prepareForReading(cb, *target);
        rhi::prepareForReading(cb, m_DepthTexture);
    }

    void onImGui() override
    {
        auto& services = *getServices();
        examples::suppressCameraWhenUsingImGui(services);

        ImGui::Begin("RayQuery Example", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted("This example renders raytraced shadows with a fragment-shader ray query.");
        ImGui::DragFloat3("Light Position", &m_LightPos.x, 0.05f, -20.0f, 20.0f);
        ImGui::ColorEdit3("Light Color", &m_LightColor.x);
        ImGui::ColorEdit3("Base Color", &m_BaseColor.x);
        ImGui::SliderFloat("Ambient Floor", &m_AmbientFloor, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Specular Strength", &m_SpecularStrength, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Specular Power", &m_SpecularPower, 1.0f, 128.0f, "%.1f");
        ImGui::SliderFloat("Shadow Factor", &m_ShadowFactor, 0.0f, 1.0f, "%.2f");
        examples::drawRenderDocCaptureButton(services);
        ImGui::End();
    }

private:
    struct DrawMesh
    {
        uint32_t  meshIndex {std::numeric_limits<uint32_t>::max()};
        glm::mat4 transform {1.0f};
    };

    struct PipelineVariant
    {
        rhi::VertexAttributes attributes;
        uint32_t              strideBytes {0};
        rhi::GraphicsPipeline pipeline;
    };

    static bool vertexAttributesEqual(const rhi::VertexAttributes& lhs, const rhi::VertexAttributes& rhs)
    {
        if (lhs.size() != rhs.size())
            return false;

        auto lit = lhs.begin();
        auto rit = rhs.begin();
        for (; lit != lhs.end(); ++lit, ++rit)
        {
            if (lit->first != rit->first)
                return false;
            const auto& la = lit->second;
            const auto& ra = rit->second;
            if (la.location != ra.location || la.type != ra.type || la.offset != ra.offset)
                return false;
        }

        return true;
    }

    static glm::mat4 localMatrix(const TransformComponent& transform)
    {
        return glm::translate(glm::mat4(1.0f), transform.position) * glm::mat4_cast(transform.rotation) *
               glm::scale(glm::mat4(1.0f), transform.scale);
    }

    static glm::mat4 worldMatrixForEntity(entt::registry& reg, entt::entity entity)
    {
        const auto* transform = reg.try_get<TransformComponent>(entity);
        const auto  local     = transform ? localMatrix(*transform) : glm::mat4(1.0f);

        const auto* hierarchy = reg.try_get<HierarchyComponent>(entity);
        if (!hierarchy || hierarchy->parent == entt::null || !reg.valid(hierarchy->parent))
            return local;

        return worldMatrixForEntity(reg, hierarchy->parent) * local;
    }

    rhi::GraphicsPipeline* pipelineForMesh(rhi::RenderDevice& rd, const resource::GpuMesh& mesh)
    {
        for (auto& variant : m_Pipelines)
        {
            if (variant.strideBytes == mesh.vertexStrideBytes &&
                vertexAttributesEqual(variant.attributes, mesh.vertexAttributes))
                return &variant.pipeline;
        }

        auto&           backendService = getServices()->require<IRenderBackendService>();
        PipelineVariant variant {
            .attributes  = mesh.vertexAttributes,
            .strideBytes = mesh.vertexStrideBytes,
            .pipeline =
                rhi::GraphicsPipeline::Builder {}
                    .setDepthFormat(rhi::PixelFormat::eDepth24_Stencil8)
                    .setColorFormats({backendService.swapchain().getPixelFormat()})
                    .setDepthStencil({.depthTest = true, .depthWrite = true, .depthCompareOp = rhi::CompareOp::eLess})
                    .setInputAssembly(mesh.vertexAttributes)
                    .setVertexStride(mesh.vertexStrideBytes)
                    .setBlending(0, {.enabled = false})
                    .setTopology(rhi::PrimitiveTopology::eTriangleList)
                    .addShader(rhi::ShaderType::eVertex, {.code = vertexCode})
                    .addShader(rhi::ShaderType::eFragment, {.code = fragmentCode})
                    .build(rd),
        };

        m_Pipelines.push_back(std::move(variant));
        return &m_Pipelines.back().pipeline;
    }

    void rebuildSceneResources(rhi::RenderDevice& rd)
    {
        m_SceneResourcesReady = true;
        m_DrawMeshes.clear();
        m_MeshHandles.clear();
        m_Pipelines.clear();
        m_TLAS = {};

        auto& services     = *getServices();
        auto& assetService = services.require<IAssetService>();
        auto& gpuResources = services.require<IGpuResourceService>();
        auto* worldService = services.tryGet<IWorldService>();
        if (!worldService)
            return;

        auto& world = worldService->world();
        auto& reg   = world.registry();
        auto& pool  = gpuResources.pool();

        std::vector<rhi::RayTracingInstance> tlasInstances;
        auto                                 view = reg.view<TransformComponent, MeshComponent>();
        for (auto entity : view)
        {
            const auto& transform = view.get<TransformComponent>(entity);
            const auto& meshComp  = view.get<MeshComponent>(entity);
            if (!meshComp.mesh.valid())
                continue;

            auto       handle    = assetService.loadMeshSync(meshComp.mesh);
            const auto meshIndex = handle.gpuIndex();
            if (!handle.ready() || meshIndex == std::numeric_limits<uint32_t>::max() || meshIndex >= pool.meshes.size())
                continue;

            auto& gpuMesh = pool.meshes[meshIndex];
            if (!gpuMesh.vertexBuffer || !gpuMesh.indexBuffer || gpuMesh.indexCount == 0 || !gpuMesh.blas)
                continue;
            if (!gpuMesh.vertexAttributes.contains(0) || !gpuMesh.vertexAttributes.contains(1))
                continue;

            const glm::mat4 worldMatrix = worldMatrixForEntity(reg, entity);
            const uint32_t  instanceID  = static_cast<uint32_t>(m_DrawMeshes.size());
            m_MeshHandles.push_back(std::move(handle));
            m_DrawMeshes.push_back(DrawMesh {
                .meshIndex = meshIndex,
                .transform = worldMatrix,
            });
            tlasInstances.push_back(rhi::RayTracingInstance {
                .blas       = &gpuMesh.blas,
                .transform  = worldMatrix,
                .instanceID = instanceID,
            });
        }

        if (tlasInstances.empty())
        {
            VULTRA_CLIENT_ERROR("[RayQuery] No scene mesh with BLAS found. Scene: {}", kSceneUri);
            return;
        }

        m_TLAS = rd.createBuildMultipleInstanceTLAS(tlasInstances);
    }

    std::vector<AssetHandle<vasset::VMesh, resource::GpuMesh>> m_MeshHandles;
    std::vector<DrawMesh>                                      m_DrawMeshes;
    std::vector<PipelineVariant>                               m_Pipelines;
    bool                                                       m_SceneResourcesReady {false};

    rhi::AccelerationStructure m_TLAS;
    rhi::Texture               m_DepthTexture;

    glm::vec3 m_LightPos {-5.0f, 5.0f, -5.0f};
    glm::vec3 m_LightColor {1.0f, 0.95f, 0.85f};
    glm::vec3 m_BaseColor {0.78f, 0.72f, 0.64f};
    float     m_AmbientFloor {0.12f};
    float     m_SpecularStrength {0.25f};
    float     m_SpecularPower {32.0f};
    float     m_ShadowFactor {0.25f};
};

class RayQueryApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "Ray Query Example"; }

    rhi::RenderDeviceFeatureFlagBits demoRenderDeviceFeatureFlag() const override
    {
        return rhi::RenderDeviceFeatureFlagBits::eRayTracing;
    }

    Ref<Renderer> makeRenderer() const override { return createRef<RayQueryRenderer>(); }

    FPSCameraController makeFPSCameraController() const override
    {
        auto controller          = DemoAppHost::makeFPSCameraController();
        controller.position      = {0.0f, 4.0f, 8.0f};
        controller.yawDegrees    = -90.0f;
        controller.pitchDegrees  = -20.0f;
        controller.orbitDistance = 8.0f;
        controller.moveSpeed     = 5.0f;
        controller.zFar          = 100.0f;
        return controller;
    }

    void onPostConfigureDemo(Engine& engine) override
    {
        auto& sceneService = engine.ctx().services.require<ISceneService>();
        auto& world        = engine.ctx().services.require<IWorldService>().world();
        auto& reg          = world.registry();

        auto root = sceneService.instantiateScene(world, kSceneUri);
        if (root == entt::null)
        {
            VULTRA_CLIENT_ERROR("[RayQuery] Failed to instantiate scene: {}", kSceneUri);
            return;
        }

        if (auto* name = reg.try_get<NameComponent>(root))
            name->name = "Ray Query Scene";
    }
};

VULTRA_DEMO_APP_MAIN(RayQueryApp)
