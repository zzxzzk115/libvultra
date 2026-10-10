#include "openpbr_luts.hpp"

#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>

namespace vultra
{
    namespace
    {
        std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& matrix)
        {
            const glm::vec4 x {matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]};
            const glm::vec4 y {matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]};
            const glm::vec4 z {matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]};
            const glm::vec4 w {matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]};
            std::array      planes {w + x, w - x, w + y, w - y, z, w - z}; // VRI depth is [0,1].
            for (auto& plane : planes)
            {
                const float length = glm::length(glm::vec3(plane));
                if (length > 0)
                {
                    plane /= length;
                }
            }
            return planes;
        }

        bool intersects(const PrimitiveBounds& bounds, const std::array<glm::vec4, 6>& planes)
        {
            for (const auto& plane : planes)
            {
                const glm::vec3 normal(plane);
                const auto      support =
                    glm::dot(normal, bounds.center) + plane.w + glm::dot(glm::abs(normal), bounds.extent);
                // Keep touching boxes despite float transform/clip-plane roundoff.
                const auto margin = 1e-4f * (1 + glm::dot(glm::abs(normal), glm::abs(bounds.center) + bounds.extent));
                if (support < -margin)
                {
                    return false;
                }
            }
            return true;
        }

        struct LightData
        {
            glm::vec4 positionRange;
            glm::vec4 directionKind;
            glm::vec4 colorIntensity;
            glm::vec4 cones;
        };

        // Keep these layouts in sync with builtin/shaders/resources/frame_block.slangh.
        struct FrameData
        {
            glm::mat4                viewProjection;
            glm::mat4                inverseViewProjection;
            glm::mat4                view;
            std::array<glm::mat4, 4> lightViewProjection;
            glm::vec4                cameraPosition;
            glm::vec4                lightDirection;
            glm::vec4                lightColor;
            glm::vec4                ambientColor;
            glm::vec4                cascadeSplits;
            glm::vec4                cascadeWidths;
            glm::vec4                cascadeDepthRanges;
            glm::vec4                shadowParameters;
            glm::vec4                options;
            glm::vec4                cameraClip;
            glm::vec4                overrides;
            glm::vec4                lightConfig; // Count, shadow-casting directional index (-1 if absent).
            std::array<LightData, kMaxRenderLights> lights;
        };

        struct MaterialData
        {
            glm::vec4  baseColor;
            glm::vec4  surface;
            glm::vec4  emission;
            glm::vec4  coat;
            glm::vec4  specular;
            glm::vec4  flags;
            glm::uvec4 meshlets; // First meshlet, count, frustum culling, debug colors.
            glm::uvec4 instance; // Primitive transform index.
        };

        static_assert(sizeof(LightData) == 64);
        static_assert(offsetof(FrameData, ambientColor) == 496);
        static_assert(offsetof(FrameData, lightConfig) == 624 && offsetof(FrameData, lights) == 640);
        static_assert(sizeof(FrameData) == 4736);
        static_assert(sizeof(MaterialData) == 128);
        static_assert(sizeof(SceneVertex) == 64 && offsetof(SceneVertex, tangent) == 48);

        constexpr std::array<VriFormat, 7> kGBufferFormats {VriFormat_RGBA32_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT};
        constexpr std::array               kGBufferNames {"gbuffer_position_metallic",
                                            "gbuffer_normal_roughness",
                                            "gbuffer_albedo_weight",
                                            "gbuffer_emission_occlusion",
                                            "gbuffer_specular",
                                            "gbuffer_geometric_normal_ior",
                                            "gbuffer_coat"};

        VriPipeline* createPipeline(Device&                        device,
                                    VriPipelineLayout*             layout,
                                    std::span<const VriShaderDesc> shaders,
                                    std::span<const VriFormat>     formats,
                                    bool                           mesh,
                                    bool                           depth,
                                    bool                           doubleSided,
                                    bool                           mirrored     = false,
                                    bool                           depthWrite   = true,
                                    VriCompareOp                   depthCompare = VriCompareOp_Less)
        {
            VriVertexStreamDesc    stream {sizeof(SceneVertex), 0, VriVertexStepRate_PerVertex};
            VriVertexAttributeDesc attributes[5] {};
            attributes[0].format = VriFormat_RGB32_SFLOAT;
            attributes[0].offset = offsetof(SceneVertex, position);
            attributes[1].format = VriFormat_RGB32_SFLOAT;
            attributes[1].offset = offsetof(SceneVertex, normal);
            attributes[2].format = VriFormat_RG32_SFLOAT;
            attributes[2].offset = offsetof(SceneVertex, uv);
            attributes[3].format = VriFormat_RGBA32_SFLOAT;
            attributes[3].offset = offsetof(SceneVertex, color);
            attributes[4].format = VriFormat_RGBA32_SFLOAT;
            attributes[4].offset = offsetof(SceneVertex, tangent);
            std::array<VriColorAttachmentDesc, 4> colors {};
            if (formats.size() > colors.size())
            {
                throw std::invalid_argument("Built-in pipeline supports at most four color attachments");
            }
            for (size_t i = 0; i < formats.size(); ++i)
            {
                colors[i].format         = formats[i];
                colors[i].colorWriteMask = VriColorWrite_RGBA;
            }
            VriGraphicsPipelineDesc desc {};
            desc.pipelineCache  = device.pipelineCache;
            desc.pipelineLayout = layout;
            desc.shaders        = shaders.data();
            desc.shaderNum      = uint32_t(shaders.size());
            if (mesh)
            {
                desc.vertexInput = {attributes, 5, &stream, 1};
            }
            desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
            desc.rasterization.cullMode  = doubleSided ? VriCullMode_None : VriCullMode_Back;
            desc.rasterization.frontFace = mirrored ? VriFrontFace_Clockwise : VriFrontFace_CounterClockwise;
            desc.rasterization.lineWidth = 1;
            desc.multisample.sampleNum   = 1;
            if (!formats.empty())
            {
                desc.outputMerger.colors   = colors.data();
                desc.outputMerger.colorNum = uint32_t(formats.size());
            }
            if (depth)
            {
                desc.depthStencil.depthTest          = VRI_TRUE;
                desc.depthStencil.depthWrite         = depthWrite ? VRI_TRUE : VRI_FALSE;
                desc.depthStencil.depthCompareOp     = depthCompare;
                desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
            }
            VriPipeline* result = nullptr;
            check(device.core.CreateGraphicsPipeline(device.handle, &desc, &result),
                  "Create built-in graphics pipeline");
            return result;
        }

        void setViewport(Device& device, VriCommandBuffer* cmd, Extent size)
        {
            const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
            const VriRect     scissor {0, 0, size.width, size.height};
            device.core.CmdSetViewports(cmd, &viewport, 1);
            device.core.CmdSetScissors(cmd, &scissor, 1);
        }
    } // namespace

    BuiltinRenderer::BuiltinRenderer(Device&      device,
                                     GpuScene&    scene,
                                     Environment& environment,
                                     VriFormat    outputFormat) :
        m_Device(device),
        m_Scene(scene),
        m_Environment(environment),
        m_OutputFormat(outputFormat)
    {
        if (outputFormat != VriFormat_RGBA8_UNORM && outputFormat != VriFormat_BGRA8_UNORM &&
            outputFormat != VriFormat_RGBA16_SFLOAT)
        {
            throw std::invalid_argument("Tone mapping output must be display UNORM or linear RGBA16_SFLOAT");
        }
        try
        {
            const VriShaderStageFlags geometryStages =
                VriShaderStage_Vertex | (scene.meshlets ? VriShaderStage_Task | VriShaderStage_Mesh : 0);
            if (scene.meshlets)
            {
                check(vriGetInterface(device.handle, VRI_INTERFACE_MESHSHADER, sizeof(m_MeshApi), &m_MeshApi),
                      "Get meshlet draw interface (requires VriFeature_MeshShader)");
            }
            m_OpenPbrLuts = createOpenPbrLuts(device);
            m_FrameBuffer = std::make_unique<Buffer>(
                device,
                VriBufferDesc {sizeof(FrameData), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
            const VriBufferViewDesc bufferView {m_FrameBuffer->handle,
                                                VriDescriptorType_ConstantBuffer,
                                                VriFormat_Unknown,
                                                0,
                                                sizeof(FrameData)};
            check(device.core.CreateBufferView(device.handle, &bufferView, &m_FrameView), "Create frame uniform view");
            const VriBufferViewDesc transformView {scene.transforms->handle,
                                                   VriDescriptorType_StructuredBuffer,
                                                   VriFormat_Unknown,
                                                   0,
                                                   scene.transforms->desc.size};
            check(device.core.CreateBufferView(device.handle, &transformView, &m_TransformView),
                  "Create primitive transform view");
            VriDescriptorRangeDesc frameRanges[20] {};
            for (uint32_t i = 0; i < 20; ++i)
            {
                frameRanges[i] = {i, 1, VriDescriptorType_Texture, geometryStages | VriShaderStage_Fragment};
            }
            frameRanges[0].descriptorType  = VriDescriptorType_ConstantBuffer;
            frameRanges[10].descriptorType = VriDescriptorType_Sampler;
            frameRanges[19]                = {19, 1, VriDescriptorType_StructuredBuffer, geometryStages};
            VriDescriptorRangeDesc materialRanges[kMaterialTextureCount + 1] {};
            for (uint32_t i = 0; i <= kMaterialTextureCount; ++i)
            {
                materialRanges[i] = {i, 1, VriDescriptorType_Texture, VriShaderStage_Fragment};
            }
            materialRanges[kMaterialTextureCount].descriptorType = VriDescriptorType_Sampler;
            materialRanges[kMaterialTextureCount].descriptorNum  = kMaterialTextureCount;
            VriDescriptorRangeDesc meshletRanges[4] {};
            for (uint32_t i = 0; i < 4; ++i)
            {
                meshletRanges[i] = {i,
                                    1,
                                    VriDescriptorType_StructuredBuffer,
                                    VriShaderStage_Task | VriShaderStage_Mesh};
            }
            VriDescriptorSetDesc  sets[3] {{0, frameRanges, 20},
                                           {1, materialRanges, kMaterialTextureCount + 1},
                                           {2, meshletRanges, 4}};
            VriPushConstantDesc   push {0, sizeof(MaterialData), geometryStages | VriShaderStage_Fragment};
            VriPipelineLayoutDesc layout {};
            layout.descriptorSets   = sets;
            layout.descriptorSetNum = scene.meshlets ? 3 : 2;
            layout.pushConstants    = &push;
            layout.pushConstantNum  = 1;
            layout.shaderStages     = push.shaderStages;
            check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Create built-in layout");

            VriSamplerDesc sampler {};
            sampler.minFilter    = VriFilter_Linear;
            sampler.magFilter    = VriFilter_Linear;
            sampler.mipmapMode   = VriMipmapMode_Linear;
            sampler.addressModeU = VriAddressMode_Repeat;
            sampler.addressModeV = VriAddressMode_Repeat;
            sampler.addressModeW = VriAddressMode_Repeat;
            sampler.maxLod       = 32;
            m_MaterialSamplers.resize(scene.samplers.size() + 1);
            for (size_t i = 0; i < m_MaterialSamplers.size(); ++i)
            {
                const auto& desc = i == 0 ? sampler : scene.samplers[i - 1];
                check(device.core.CreateSampler(device.handle, &desc, &m_MaterialSamplers[i]),
                      "Create material sampler");
            }
            sampler.addressModeV = VriAddressMode_ClampToEdge;
            sampler.addressModeW = VriAddressMode_ClampToEdge;
            check(device.core.CreateSampler(device.handle, &sampler, &m_EnvironmentSampler),
                  "Create environment sampler");
            const auto count = uint32_t(scene.materials.size());
            m_ShaderMaterials.resize(count);
            m_ShaderPipelines.resize(count);
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum    = count + (scene.meshlets ? 2 : 1);
            pool.textureMaxNum          = count * kMaterialTextureCount + 17;
            pool.samplerMaxNum          = count * kMaterialTextureCount + 1;
            pool.constantBufferMaxNum   = 1;
            pool.structuredBufferMaxNum = 1 + (scene.meshlets ? 4 : 0);
            check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Create built-in descriptor pool");
            check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_FrameSet, 1), "Allocate frame set");
            if (scene.meshlets)
            {
                check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 2, &m_MeshletSet, 1),
                      "Allocate meshlet set");
                VriDescriptorRangeUpdateDesc updates[4] {};
                const VriDescriptor*         views[4] {};
                for (uint32_t i = 0; i < 4; ++i)
                {
                    views[i]   = scene.meshlets->views[i];
                    updates[i] = {&views[i], 1};
                }
                device.core.UpdateDescriptorRanges(m_MeshletSet, 0, 4, updates);
            }
            m_MaterialSets.resize(count);
            check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 1, m_MaterialSets.data(), count),
                  "Allocate material sets");
            for (uint32_t material = 0; material < count; ++material)
            {
                const auto&                  source = scene.materials[material];
                const MaterialTexture        slots[] {source.baseColorTexture,
                                                      source.metallicRoughnessTexture,
                                                      source.normalTexture,
                                                      source.occlusionTexture,
                                                      source.emissionTexture,
                                                      source.specularTexture,
                                                      source.specularColorTexture};
                const VriDescriptor*         textures[kMaterialTextureCount] {};
                const VriDescriptor*         samplers[kMaterialTextureCount] {};
                VriDescriptorRangeUpdateDesc updates[kMaterialTextureCount + 1] {};
                for (uint32_t slot = 0; slot < kMaterialTextureCount; ++slot)
                {
                    if (slots[slot].sampler < -1 ||
                        (slots[slot].sampler >= 0 && size_t(slots[slot].sampler) >= scene.samplers.size()))
                    {
                        throw std::invalid_argument("Invalid material sampler index");
                    }
                    textures[slot] = scene.materialTextures[material][slot]->view();
                    samplers[slot] = m_MaterialSamplers[size_t(slots[slot].sampler + 1)];
                    updates[slot]  = {&textures[slot], 1};
                }
                updates[kMaterialTextureCount] = {samplers, kMaterialTextureCount};
                device.core.UpdateDescriptorRanges(m_MaterialSets[material], 0, kMaterialTextureCount + 1, updates);
            }
            auto pipeline =
                [&](const char* file, bool mesh, bool depth, VriFormat format, bool doubleSided, bool mirrored = false)
            {
                return std::make_unique<ShaderPipeline>(
                    device,
                    std::filesystem::path("builtin/shaders/passes") / file,
                    std::vector<ShaderEntry> {{mesh ? "vertexMain" : "screenVertex", VriShaderStage_Vertex},
                                              {"fragmentMain", VriShaderStage_Fragment}},
                    [this, mesh, depth, format, doubleSided, mirrored](std::span<const VriShaderDesc> shaders)
                    {
                        const std::array singleFormat {format};
                        return createPipeline(m_Device,
                                              m_Layout,
                                              shaders,
                                              format == VriFormat_Unknown ? std::span<const VriFormat> {} :
                                                                            std::span<const VriFormat> {singleFormat},
                                              mesh,
                                              depth,
                                              doubleSided,
                                              mirrored);
                    },
                    "builtin/shaders",
                    std::vector<std::filesystem::path> {"builtin/shaders", "external"});
            };
            for (uint32_t variant = 0; variant < 4; ++variant)
            {
                const bool doubleSided = (variant & 1) != 0;
                const bool mirrored    = (variant & 2) != 0;
                m_Shadow[variant]      = pipeline("shadow.slang", true, true, VriFormat_Unknown, doubleSided, mirrored);
                m_Forward[variant] =
                    pipeline("forward.slang", true, true, VriFormat_RGBA16_SFLOAT, doubleSided, mirrored);
                for (uint32_t stage = 0; stage < 2; ++stage)
                {
                    auto& target = stage == 0 ? m_GBufferBase[variant] : m_GBufferMaterial[variant];
                    target       = std::make_unique<ShaderPipeline>(
                        device,
                        "builtin/shaders/passes/gbuffer.slang",
                        std::vector<ShaderEntry> {
                            {"vertexMain", VriShaderStage_Vertex},
                            {stage == 0 ? "fragmentBase" : "fragmentMaterial", VriShaderStage_Fragment}},
                        [this, stage, doubleSided, mirrored](std::span<const VriShaderDesc> shaders)
                        {
                            const auto formats = stage == 0 ? std::span(kGBufferFormats).first(4) :
                                                                    std::span(kGBufferFormats).subspan(4);
                            return createPipeline(m_Device,
                                                  m_Layout,
                                                  shaders,
                                                  formats,
                                                  true,
                                                  true,
                                                  doubleSided,
                                                  mirrored,
                                                  stage == 0,
                                                  stage == 0 ? VriCompareOp_Less : VriCompareOp_Equal);
                        },
                        "builtin/shaders",
                        std::vector<std::filesystem::path> {"builtin/shaders", "external"});
                }
                if (scene.meshlets)
                {
                    m_MeshForward[variant] = std::make_unique<ShaderPipeline>(
                        device,
                        "builtin/shaders/passes/meshlet_forward.slang",
                        std::vector<ShaderEntry> {{"taskMain", VriShaderStage_Task},
                                                  {"meshMain", VriShaderStage_Mesh},
                                                  {"fragmentMain", VriShaderStage_Fragment}},
                        [this, doubleSided, mirrored](std::span<const VriShaderDesc> shaders)
                        {
                            return createPipeline(m_Device,
                                                  m_Layout,
                                                  shaders,
                                                  std::array {VriFormat_RGBA16_SFLOAT},
                                                  false,
                                                  true,
                                                  doubleSided,
                                                  mirrored);
                        },
                        "builtin/shaders",
                        std::vector<std::filesystem::path> {"builtin/shaders", "external"});
                }
            }
            m_Skybox           = pipeline("skybox.slang", false, false, VriFormat_RGBA16_SFLOAT, true);
            m_ToneMapping      = std::make_unique<ToneMappingPass>(device, m_OutputFormat);
            m_DeferredLighting = pipeline("deferred_lighting.slang", false, false, VriFormat_RGBA16_SFLOAT, true);
        }
        catch (...)
        {
            release();
            throw;
        }
    }

    BuiltinRenderer::~BuiltinRenderer()
    {
        m_Device.waitIdle();
        release();
    }

    void BuiltinRenderer::release()
    {
        for (uint32_t variant = 0; variant < 4; ++variant)
        {
            m_Shadow[variant].reset();
            m_Forward[variant].reset();
            m_MeshForward[variant].reset();
            m_GBufferBase[variant].reset();
            m_GBufferMaterial[variant].reset();
        }
        m_Skybox.reset();
        m_ToneMapping.reset();
        m_DeferredLighting.reset();
        if (m_Pool)
        {
            m_Device.core.DestroyDescriptorPool(m_Pool);
        }
        for (auto* sampler : m_MaterialSamplers)
        {
            if (sampler)
            {
                m_Device.core.DestroyDescriptor(sampler);
            }
        }
        for (auto* descriptor : {m_FrameView, m_TransformView, m_EnvironmentSampler})
        {
            if (descriptor)
            {
                m_Device.core.DestroyDescriptor(descriptor);
            }
        }
        if (m_Layout)
        {
            m_Device.core.DestroyPipelineLayout(m_Layout);
        }
    }

    void BuiltinRenderer::drawScene(VriCommandBuffer* cmd, GeometryPass pass, uint32_t cascade)
    {
        const bool meshShading = settings.meshShading && pass == GeometryPass::eForward;
        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
        if (!meshShading)
        {
            const VriVertexBufferBinding vertices {m_Scene.vertices->handle, 0};
            m_Device.core.CmdSetVertexBuffers(cmd, 0, &vertices, 1);
            m_Device.core.CmdSetIndexBuffer(cmd, m_Scene.indices->handle, 0, VriIndexType_UInt32);
        }
        VriPipeline* activePipeline = nullptr;
        const auto&  visible        = m_VisiblePrimitives[pass == GeometryPass::eShadow ? cascade + 1 : 0];
        for (const uint32_t i : visible)
        {
            const auto& primitive = m_Scene.primitives[i];
            const auto& material  = m_Scene.materials.at(primitive.material);
            if (auto* shader = m_ShaderMaterials[primitive.material])
            {
                const auto purpose  = meshShading ? 4u : uint32_t(pass);
                const auto mirrored = m_Scene.primitiveMirrored(uint32_t(i)) ? 1u : 0u;
                shader->bind(cmd, m_ShaderPipelines[primitive.material][mirrored * 5 + purpose]);
                const VriVertexBufferBinding vertices {m_Scene.vertices->handle, 0};
                m_Device.core.CmdSetVertexBuffers(cmd, 0, &vertices, 1);
                m_Device.core.CmdSetIndexBuffer(cmd, m_Scene.indices->handle, 0, VriIndexType_UInt32);

                struct GameDraw
                {
                    glm::uvec4 instance;
                    glm::uvec4 meshlets;
                } parameters {{uint32_t(i), cascade, 0, 0}, {0, 0, 0, 0}};

                if (meshShading)
                {
                    const auto range    = m_Scene.meshlets->primitives[i];
                    parameters.meshlets = {range.first, range.count, settings.meshletCulling ? 1u : 0u, 0};
                }
                m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
                if (meshShading)
                {
                    m_MeshApi.CmdDrawMeshTasks(cmd,
                                               (parameters.meshlets.y + kMeshletTaskSize - 1) / kMeshletTaskSize,
                                               1,
                                               1);
                }
                else
                {
                    const VriDrawIndexedDesc draw {primitive.indexCount, 1, primitive.firstIndex, 0, 0};
                    m_Device.core.CmdDrawIndexed(cmd, &draw);
                }
                // The next imported primitive restores its descriptors after binding its graphics pipeline.
                m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
                activePipeline = nullptr;
                continue;
            }
            const uint32_t variant =
                (material.doubleSided ? 1u : 0u) + (m_Scene.primitiveMirrored(uint32_t(i)) ? 2u : 0u);
            VriPipeline* pipeline = nullptr;
            switch (pass)
            {
                case GeometryPass::eShadow:
                    pipeline = m_Shadow[variant]->handle();
                    break;
                case GeometryPass::eForward:
                    pipeline = m_Forward[variant]->handle();
                    break;
                case GeometryPass::eGBufferBase:
                    pipeline = m_GBufferBase[variant]->handle();
                    break;
                case GeometryPass::eGBufferMaterial:
                    pipeline = m_GBufferMaterial[variant]->handle();
                    break;
            }
            if (meshShading)
            {
                pipeline = m_MeshForward[variant]->handle();
            }
            if (pipeline != activePipeline)
            {
                m_Device.core.CmdSetPipeline(cmd, pipeline);
                // VRI binds descriptors at the current pipeline's bind point, which may previously be compute.
                m_Device.core.CmdSetDescriptorSet(cmd, 0, m_FrameSet);
                if (meshShading)
                {
                    m_Device.core.CmdSetDescriptorSet(cmd, 2, m_MeshletSet);
                }
                activePipeline = pipeline;
            }
            float normalMode = 0;
            if (material.normalTexture.image >= 0)
            {
                const auto format = m_Scene.materialTextures[primitive.material][2]->desc.format;
                normalMode        = format == VriFormat_BC5_UNORM || format == VriFormat_RG8_UNORM ? 2.0f : 1.0f;
            }
            MaterialData parameters {
                material.baseColor,
                {material.baseMetalness, material.specularRoughness, material.specularIor, material.normalScale},
                {material.emissionColor * material.emissionLuminance, material.occlusionStrength},
                {material.coatWeight, material.coatRoughness, material.coatIor, material.specularWeight},
                {material.specularColor, material.baseDiffuseRoughness},
                {material.alphaCutoff, material.baseWeight, float(cascade), normalMode},
                {0, 0, 0, 0},
                {uint32_t(i), 0, 0, 0}};
            if (meshShading)
            {
                const auto range    = m_Scene.meshlets->primitives[i];
                parameters.meshlets = {range.first,
                                       range.count,
                                       settings.meshletCulling ? 1u : 0u,
                                       settings.meshletColors ? 1u : 0u};
            }
            m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
            m_Device.core.CmdSetDescriptorSet(cmd, 1, m_MaterialSets[primitive.material]);
            if (meshShading)
            {
                if (parameters.meshlets.y != 0)
                {
                    m_MeshApi.CmdDrawMeshTasks(cmd,
                                               (parameters.meshlets.y + kMeshletTaskSize - 1) / kMeshletTaskSize,
                                               1,
                                               1);
                }
            }
            else
            {
                const VriDrawIndexedDesc draw {primitive.indexCount, 1, primitive.firstIndex, 0, 0};
                m_Device.core.CmdDrawIndexed(cmd, &draw);
            }
        }
    }

    void BuiltinRenderer::drawFullscreen(VriCommandBuffer* cmd, VriPipeline* pipeline)
    {
        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
        m_Device.core.CmdSetPipeline(cmd, pipeline);
        m_Device.core.CmdSetDescriptorSet(cmd, 0, m_FrameSet);
        const VriDrawDesc draw {3, 1, 0, 0};
        m_Device.core.CmdDraw(cmd, &draw);
    }

    BuiltinRenderer::ShadowMaps BuiltinRenderer::addShadowPasses(RenderGraph& graph)
    {
        ShadowMaps   maps;
        const Extent size {settings.shadowResolution, settings.shadowResolution};
        for (uint32_t cascade = 0; cascade < 4; ++cascade)
        {
            auto desc = depthTexture(size);
            desc.usage |= VriTextureUsage_TransferSrc;
            maps[cascade] = graph.createTexture("shadow_" + std::to_string(cascade), desc);
            graph.addPass("Shadow " + std::to_string(cascade),
                          {{maps[cascade], Usage::eDepthWrite}},
                          [this, cascade, resource = maps[cascade], size](auto* cmd, auto& resources)
                          {
                              const auto& key = m_PreparedShadowKeys[cascade];
                              if (m_CacheShadows && m_ShadowCache[cascade] && *m_ShadowCache[cascade] == key)
                              {
                                  m_ShadowDrawn[cascade] = false;
                                  return;
                              }
                              m_ShadowCache[cascade].reset();
                              m_ShadowDrawn[cascade] = settings.shadowFilter != ShadowFilter::eDisabled;
                              VriAttachmentDesc depth {};
                              depth.view                          = resources.getTexture(resource).view();
                              depth.loadOp                        = VriAttachmentLoadOp_Clear;
                              depth.storeOp                       = VriAttachmentStoreOp_Store;
                              depth.clearValue.depthStencil.depth = 1;
                              VriAttachmentsDesc attachments {};
                              attachments.depth      = &depth;
                              attachments.renderArea = {0, 0, size.width, size.height};
                              attachments.layerNum   = 1;
                              m_Device.core.CmdBeginRendering(cmd, &attachments);
                              setViewport(m_Device, cmd, size);
                              if (settings.shadowFilter != ShadowFilter::eDisabled)
                              {
                                  drawScene(cmd, GeometryPass::eShadow, cascade);
                              }
                              m_Device.core.CmdEndRendering(cmd);
                              if (m_CacheShadows)
                              {
                                  m_RecordedShadows[cascade] = key;
                              }
                          });
        }
        return maps;
    }

    void BuiltinRenderer::addSkyboxPass(RenderGraph& graph, RenderGraph::Resource hdr)
    {
        graph.addPass("Skybox",
                      {{hdr, Usage::eColorWrite}},
                      [this, hdr](auto* cmd, auto& resources)
                      {
                          auto&       target = resources.getTexture(hdr);
                          const float clear[4] {0, 0, 0, 1};
                          beginColorPass(m_Device, cmd, target.view(), {target.desc.width, target.desc.height}, clear);
                          drawFullscreen(cmd, m_Skybox->handle());
                          m_Device.core.CmdEndRendering(cmd);
                      });
    }

    void BuiltinRenderer::addForwardPass(RenderGraph&          graph,
                                         RenderGraph::Resource hdr,
                                         RenderGraph::Resource depth,
                                         ShadowMaps            shadows)
    {
        graph.addPass("Forward OpenPBR",
                      {{hdr, Usage::eColorReadWrite},
                       {depth, Usage::eDepthWrite},
                       {shadows[0], Usage::eSampled},
                       {shadows[1], Usage::eSampled},
                       {shadows[2], Usage::eSampled},
                       {shadows[3], Usage::eSampled}},
                      [this, hdr, depth](auto* cmd, auto& resources)
                      {
                          auto&             target = resources.getTexture(hdr);
                          const Extent      size {target.desc.width, target.desc.height};
                          VriAttachmentDesc colorAttachment {};
                          colorAttachment.view    = target.view();
                          colorAttachment.loadOp  = VriAttachmentLoadOp_Load;
                          colorAttachment.storeOp = VriAttachmentStoreOp_Store;
                          VriAttachmentDesc depthAttachment {};
                          depthAttachment.view                          = resources.getTexture(depth).view();
                          depthAttachment.loadOp                        = VriAttachmentLoadOp_Clear;
                          depthAttachment.storeOp                       = VriAttachmentStoreOp_Store;
                          depthAttachment.clearValue.depthStencil.depth = 1;
                          VriAttachmentsDesc attachments {};
                          attachments.colors     = &colorAttachment;
                          attachments.colorNum   = 1;
                          attachments.depth      = &depthAttachment;
                          attachments.renderArea = {0, 0, size.width, size.height};
                          attachments.layerNum   = 1;
                          m_Device.core.CmdBeginRendering(cmd, &attachments);
                          setViewport(m_Device, cmd, size);
                          drawScene(cmd, GeometryPass::eForward);
                          m_Device.core.CmdEndRendering(cmd);
                      });
    }

    BuiltinRenderer::GBuffer
    BuiltinRenderer::addGBufferPasses(RenderGraph& graph, RenderGraph::Resource depth, Extent size)
    {
        const auto depthInfo = graph.resourceInfo(depth);
        if (!depthInfo.isTexture || depthInfo.textureDesc.format != VriFormat_D32_SFLOAT ||
            depthInfo.textureDesc.width != size.width || depthInfo.textureDesc.height != size.height)
        {
            throw std::invalid_argument("G-buffer depth requires D32F at the view extent");
        }
        GBuffer gbuffer {};
        for (size_t i = 0; i < gbuffer.size(); ++i)
        {
            gbuffer[i] = graph.createTexture(kGBufferNames[i], colorTexture(size, kGBufferFormats[i]));
        }
        graph.addPass("G-buffer geometry",
                      {{gbuffer[0], Usage::eColorWrite},
                       {gbuffer[1], Usage::eColorWrite},
                       {gbuffer[2], Usage::eColorWrite},
                       {gbuffer[3], Usage::eColorWrite},
                       {depth, Usage::eDepthWrite}},
                      [this, gbuffer, depth, size](auto* cmd, auto& resources)
                      {
                          std::array<VriAttachmentDesc, 4> colors {};
                          for (size_t i = 0; i < colors.size(); ++i)
                          {
                              colors[i].view    = resources.getTexture(gbuffer[i]).view();
                              colors[i].loadOp  = VriAttachmentLoadOp_Clear;
                              colors[i].storeOp = VriAttachmentStoreOp_Store;
                          }
                          colors[0].clearValue.color.f32[3] = -1; // No geometry at this pixel.
                          VriAttachmentDesc depthAttachment {};
                          depthAttachment.view                          = resources.getTexture(depth).view();
                          depthAttachment.loadOp                        = VriAttachmentLoadOp_Clear;
                          depthAttachment.storeOp                       = VriAttachmentStoreOp_Store;
                          depthAttachment.clearValue.depthStencil.depth = 1;
                          VriAttachmentsDesc attachments {};
                          attachments.colors     = colors.data();
                          attachments.colorNum   = uint32_t(colors.size());
                          attachments.depth      = &depthAttachment;
                          attachments.renderArea = {0, 0, size.width, size.height};
                          attachments.layerNum   = 1;
                          m_Device.core.CmdBeginRendering(cmd, &attachments);
                          setViewport(m_Device, cmd, size);
                          drawScene(cmd, GeometryPass::eGBufferBase);
                          m_Device.core.CmdEndRendering(cmd);
                      });
        graph.addPass("G-buffer material",
                      {{gbuffer[4], Usage::eColorWrite},
                       {gbuffer[5], Usage::eColorWrite},
                       {gbuffer[6], Usage::eColorWrite},
                       {depth, Usage::eDepthRead}},
                      [this, gbuffer, depth, size](auto* cmd, auto& resources)
                      {
                          std::array<VriAttachmentDesc, 3> colors {};
                          for (size_t i = 0; i < colors.size(); ++i)
                          {
                              colors[i].view    = resources.getTexture(gbuffer[i + 4]).view();
                              colors[i].loadOp  = VriAttachmentLoadOp_Clear;
                              colors[i].storeOp = VriAttachmentStoreOp_Store;
                          }
                          VriAttachmentDesc depthAttachment {};
                          depthAttachment.view    = resources.getTexture(depth).view();
                          depthAttachment.loadOp  = VriAttachmentLoadOp_Load;
                          depthAttachment.storeOp = VriAttachmentStoreOp_Store;
                          VriAttachmentsDesc attachments {};
                          attachments.colors     = colors.data();
                          attachments.colorNum   = uint32_t(colors.size());
                          attachments.depth      = &depthAttachment;
                          attachments.renderArea = {0, 0, size.width, size.height};
                          attachments.layerNum   = 1;
                          m_Device.core.CmdBeginRendering(cmd, &attachments);
                          setViewport(m_Device, cmd, size);
                          drawScene(cmd, GeometryPass::eGBufferMaterial);
                          m_Device.core.CmdEndRendering(cmd);
                      });
        return gbuffer;
    }

    void BuiltinRenderer::addDeferredLightingPass(RenderGraph&          graph,
                                                  RenderGraph::Resource hdr,
                                                  GBuffer               gbuffer,
                                                  ShadowMaps            shadows)
    {
        const auto   info = graph.resourceInfo(hdr);
        const Extent size {info.textureDesc.width, info.textureDesc.height};
        if (!info.isTexture || info.textureDesc.format != VriFormat_RGBA16_SFLOAT)
        {
            throw std::invalid_argument("Deferred lighting requires a linear RGBA16F target");
        }
        for (size_t i = 0; i < gbuffer.size(); ++i)
        {
            const auto source = graph.resourceInfo(gbuffer[i]);
            if (!source.isTexture || source.textureDesc.format != kGBufferFormats[i] ||
                source.textureDesc.width != size.width || source.textureDesc.height != size.height)
            {
                throw std::invalid_argument(std::string("Deferred lighting G-buffer contract mismatch: ") +
                                            kGBufferNames[i]);
            }
        }
        const auto shadowInfo = graph.resourceInfo(shadows[0]);
        for (const auto shadow : shadows)
        {
            const auto source = graph.resourceInfo(shadow);
            if (!source.isTexture || source.textureDesc.format != VriFormat_D32_SFLOAT ||
                source.textureDesc.width != shadowInfo.textureDesc.width ||
                source.textureDesc.height != source.textureDesc.width)
            {
                throw std::invalid_argument("Deferred lighting requires four equally sized square D32F shadow maps");
            }
        }
        graph.addPass("Deferred OpenPBR",
                      {{hdr, Usage::eColorReadWrite},
                       {gbuffer[0], Usage::eSampled},
                       {gbuffer[1], Usage::eSampled},
                       {gbuffer[2], Usage::eSampled},
                       {gbuffer[3], Usage::eSampled},
                       {gbuffer[4], Usage::eSampled},
                       {gbuffer[5], Usage::eSampled},
                       {gbuffer[6], Usage::eSampled},
                       {shadows[0], Usage::eSampled},
                       {shadows[1], Usage::eSampled},
                       {shadows[2], Usage::eSampled},
                       {shadows[3], Usage::eSampled}},
                      [this, hdr, size](auto* cmd, auto& resources)
                      {
                          beginColorPass(m_Device, cmd, resources.getTexture(hdr).view(), size);
                          drawFullscreen(cmd, m_DeferredLighting->handle());
                          m_Device.core.CmdEndRendering(cmd);
                      });
    }

    RenderGraph::Resource
    BuiltinRenderer::addToneMappingPass(RenderGraph& graph, RenderGraph::Resource hdr, Extent size)
    {
        const auto info = graph.resourceInfo(hdr);
        if (info.textureDesc.width != size.width || info.textureDesc.height != size.height)
        {
            throw std::invalid_argument("Tone mapping extent differs from its input");
        }
        const std::array inputs {hdr};
        return m_ToneMapping->addPasses(graph, "Tone mapping", inputs, m_ToneParameters).front();
    }

    BuiltinRenderer::Outputs BuiltinRenderer::addScenePasses(RenderGraph& graph, Extent size)
    {
        if (settings.path == RenderPath::eReferencePathTracing)
        {
            throw std::invalid_argument("Use ReferencePathTracer to build reference scene outputs");
        }

        Outputs outputs;
        outputs.path    = settings.path;
        outputs.shadows = addShadowPasses(graph);
        outputs.hdr     = graph.createTexture("scene_hdr", colorTexture(size, VriFormat_RGBA16_SFLOAT));
        auto depthDesc  = depthTexture(size);
        depthDesc.usage |= VriTextureUsage_TransferSrc;
        outputs.depth = graph.createTexture("scene_depth", depthDesc);
        addSkyboxPass(graph, outputs.hdr);
        switch (outputs.path)
        {
            case RenderPath::eNaiveDeferred:
                if (settings.meshShading)
                {
                    throw std::invalid_argument("NaiveDeferred currently requires indexed geometry");
                }
                outputs.gbuffer = addGBufferPasses(graph, outputs.depth, size);
                addDeferredLightingPass(graph, outputs.hdr, outputs.gbuffer, outputs.shadows);
                break;
            case RenderPath::eNaiveForward:
                addForwardPass(graph, outputs.hdr, outputs.depth, outputs.shadows);
                break;
            default:
                throw std::invalid_argument("Unsupported built-in render path");
        }
        return outputs;
    }

    BuiltinRenderer::Outputs BuiltinRenderer::addPasses(RenderGraph& graph, Extent size)
    {
        auto outputs  = addScenePasses(graph, size);
        outputs.color = addToneMappingPass(graph, outputs.hdr, size);
        return outputs;
    }

    void BuiltinRenderer::prepare(const RenderCamera&                         camera,
                                  RenderGraph&                                graph,
                                  const Outputs&                              outputs,
                                  std::optional<std::span<const RenderLight>> lights,
                                  float                                       environmentIntensity)
    {
        const auto effectiveEnvironmentIntensity = settings.environmentIntensity * environmentIntensity;
        if (!std::isfinite(settings.ambientColor.x) || !std::isfinite(settings.ambientColor.y) ||
            !std::isfinite(settings.ambientColor.z) || glm::any(glm::lessThan(settings.ambientColor, glm::vec3(0))))
        {
            throw std::invalid_argument("Renderer ambient color must be nonnegative and finite");
        }
        if (!std::isfinite(environmentIntensity) || environmentIntensity < 0 ||
            !std::isfinite(effectiveEnvironmentIntensity) || effectiveEnvironmentIntensity < 0)
        {
            throw std::invalid_argument("Renderer environment intensity must be nonnegative and finite");
        }
        if (settings.meshShading && !m_MeshForward[0])
        {
            throw std::logic_error("Mesh shading requires a GpuScene created with meshlets");
        }
        FrameData data {};
        auto      directionToLight = settings.directionToLight;
        auto      lightColor       = settings.lightColor;
        auto      lightIntensity   = settings.lightIntensity;
        data.lightConfig.y         = -1;
        if (lights)
        {
            if (lights->size() > kMaxRenderLights)
            {
                throw std::invalid_argument("Renderer exceeds its 64-light limit");
            }
            lightIntensity     = 0;
            data.lightConfig.x = float(lights->size());
            for (size_t i = 0; i < lights->size(); ++i)
            {
                const auto& light  = (*lights)[i];
                const auto  finite = [](glm::vec3 value)
                {
                    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
                };
                if (light.kind > RenderLightKind::eSpot || !finite(light.position) || !finite(light.directionToLight) ||
                    !finite(light.color) || glm::any(glm::lessThan(light.color, glm::vec3(0))) ||
                    !std::isfinite(light.intensity) || light.intensity < 0 || !std::isfinite(light.range) ||
                    light.range <= 0 || !std::isfinite(light.innerCone) || !std::isfinite(light.outerCone) ||
                    light.innerCone < 0 || light.innerCone >= light.outerCone ||
                    light.outerCone >= std::numbers::pi_v<float> * 0.5f)
                {
                    throw std::invalid_argument("Invalid render light at index " + std::to_string(i));
                }
                auto direction = glm::vec3(0, 0, 1);
                if (light.kind != RenderLightKind::ePoint)
                {
                    const auto length = glm::length(light.directionToLight);
                    if (!std::isfinite(length) || length == 0)
                    {
                        throw std::invalid_argument("Render light direction is singular at index " + std::to_string(i));
                    }
                    direction = light.directionToLight / length;
                }
                data.lights[i] = {{light.position, light.range},
                                  {direction, float(light.kind)},
                                  {light.color, light.intensity},
                                  {std::cos(light.innerCone), std::cos(light.outerCone), 0, 0}};
                if (light.kind == RenderLightKind::eDirectional && data.lightConfig.y < 0)
                {
                    data.lightConfig.y = float(i);
                    directionToLight   = direction;
                    lightColor         = light.color;
                    lightIntensity     = light.intensity;
                }
            }
        }
        const auto shadowResolution = outputs.shadows[0].graph ?
                                          graph.resourceInfo(outputs.shadows[0]).textureDesc.width :
                                          settings.shadowResolution;
        const auto cascades         = calculateCascades(camera,
                                                directionToLight,
                                                m_Scene.center,
                                                m_Scene.radius,
                                                shadowResolution,
                                                settings.splitLambda);
        data.viewProjection         = camera.projection * camera.view;
        data.inverseViewProjection  = glm::inverse(data.viewProjection);
        data.view                   = camera.view;
        data.lightViewProjection    = cascades.viewProjection;
        data.cameraPosition         = glm::inverse(camera.view)[3];
        m_RecordedShadows           = {};
        // Game vertex/surface programs may depend on arbitrary resources; their shadows cannot be cached here.
        m_CacheShadows = settings.cacheShadows && std::ranges::none_of(m_ShaderMaterials,
                                                                       [](const auto* material)
                                                                       {
                                                                           return material != nullptr;
                                                                       });
        if (m_CacheShadows)
        {
            if (graph.aliasesTransients())
            {
                throw std::logic_error("Cached shadow maps require a graph without transient aliasing");
            }
            if (m_ShadowMaterials.size() != m_Scene.materials.size())
            {
                m_ShadowMaterials.resize(m_Scene.materials.size());
                invalidateShadowCache();
            }
            for (size_t i = 0; i < m_ShadowMaterials.size(); ++i)
            {
                const auto&          material = m_Scene.materials[i];
                const ShadowMaterial current {material.baseColor.a,
                                              material.alphaCutoff,
                                              material.baseColorTexture.sampler,
                                              material.doubleSided,
                                              m_Scene.materialTextures[i][0]->view()};
                if (current != m_ShadowMaterials[i])
                {
                    invalidateShadowCache();
                    m_ShadowMaterials[i] = current;
                }
            }
        }
        else
        {
            invalidateShadowCache();
        }
        std::array<uint64_t, 4> shadowPrograms;
        for (size_t i = 0; i < shadowPrograms.size(); ++i)
        {
            shadowPrograms[i]       = m_Shadow[i]->generation();
            m_PreparedShadowKeys[i] = {outputs.shadows[i].graph,
                                       outputs.shadows[i].index,
                                       data.lightViewProjection[i],
                                       m_Scene.transformRevision(),
                                       {},
                                       settings.shadowFilter != ShadowFilter::eDisabled};
        }
        for (auto& key : m_PreparedShadowKeys)
        {
            key.programs = shadowPrograms;
        }
        for (size_t volume = 0; volume < m_VisiblePrimitives.size(); ++volume)
        {
            auto& visible = m_VisiblePrimitives[volume];
            visible.clear();
            if (volume &&
                (!outputs.shadows[volume - 1].graph || !graph.resourceInfo(outputs.shadows[volume - 1]).active))
            {
                continue;
            }
            if (volume && m_CacheShadows && m_ShadowCache[volume - 1] &&
                *m_ShadowCache[volume - 1] == m_PreparedShadowKeys[volume - 1])
            {
                continue;
            }
            const auto planes = frustumPlanes(volume ? data.lightViewProjection[volume - 1] : data.viewProjection);
            visible.reserve(m_Scene.primitives.size());
            for (uint32_t index = 0; index < m_Scene.primitives.size(); ++index)
            {
                // Explicit game vertex programs may displace vertices beyond the imported bounds.
                if (m_Scene.primitives[index].indexCount && (m_ShaderMaterials[m_Scene.primitives[index].material] ||
                                                             intersects(m_Scene.primitiveBounds(index), planes)))
                {
                    visible.push_back(index);
                }
            }
        }
        data.lightDirection     = {glm::normalize(directionToLight), lightIntensity};
        data.lightColor         = {lightColor, effectiveEnvironmentIntensity};
        data.ambientColor       = {settings.ambientColor, 0};
        data.cascadeSplits      = cascades.splits;
        data.cascadeWidths      = cascades.worldWidths;
        data.cascadeDepthRanges = cascades.depthRanges;
        data.shadowParameters   = {settings.shadowBias,
                                   settings.normalBias,
                                   settings.sunAngularRadius,
                                   float(settings.shadowFilter)};
        data.options            = {settings.ibl ? 1.0f : 0.0f,
                        settings.skybox ? 1.0f : 0.0f,
                        settings.exposure,
                        float(m_Environment.specular->desc.mipNum - 1)};
        data.cameraClip  = {camera.nearPlane,
                            camera.farPlane,
                            std::clamp(settings.cascadeBlend, 0.0f, 0.15f),
                            float(settings.debugMode)};
        m_ToneParameters = {settings.exposure,
                            settings.meshShading && settings.meshletColors ? 1.0 : 0.0,
                            double(settings.toneOperator)};
        data.overrides   = {settings.roughnessOverride,
                            settings.metalnessOverride,
                          m_OutputFormat == VriFormat_RGBA16_SFLOAT ? 0.0f : 1.0f,
                          settings.meshShading && settings.meshletColors ? 1.0f : 0.0f};
        auto* mapped     = m_Device.core.MapBuffer(m_FrameBuffer->handle, 0, sizeof(data));
        if (!mapped)
        {
            throw std::runtime_error("Map renderer frame data");
        }
        std::memcpy(mapped, &data, sizeof(data));
        m_Device.core.UnmapBuffer(m_FrameBuffer->handle);
        auto resourceView = [&](RenderGraph::Resource resource) -> const VriDescriptor*
        {
            // Culled stages do not read these slots, but the shared descriptor set still needs valid views.
            return resource.graph && graph.resourceInfo(resource).active ? graph.getTexture(resource).view() :
                                                                           m_Environment.radiance->view();
        };
        const VriDescriptor* descriptors[20] {m_FrameView,
                                              resourceView(outputs.shadows[0]),
                                              resourceView(outputs.shadows[1]),
                                              resourceView(outputs.shadows[2]),
                                              resourceView(outputs.shadows[3]),
                                              m_Environment.radiance->view(),
                                              m_Environment.diffuse->view(),
                                              m_Environment.specular->view(),
                                              m_Environment.brdfLut->view(),
                                              resourceView(outputs.hdr),
                                              m_EnvironmentSampler,
                                              m_OpenPbrLuts->view()};
        for (size_t i = 0; i < outputs.gbuffer.size(); ++i)
        {
            descriptors[i + 12] = outputs.path == RenderPath::eNaiveDeferred ? resourceView(outputs.gbuffer[i]) :
                                                                               resourceView(outputs.hdr);
        }
        descriptors[19] = m_TransformView;
        VriDescriptorRangeUpdateDesc updates[20] {};
        for (uint32_t i = 0; i < 20; ++i)
        {
            updates[i].descriptors   = &descriptors[i];
            updates[i].descriptorNum = 1;
        }
        m_Device.core.UpdateDescriptorRanges(m_FrameSet, 0, 20, updates);

        for (uint32_t slot = 0; slot < m_ShaderMaterials.size(); ++slot)
        {
            if (auto* material = m_ShaderMaterials[slot])
            {
                m_ShaderPipelines[slot] = prepareShaderMaterial(*material, graph, outputs);
            }
        }
    }

    std::array<VriPipeline*, 10>
    BuiltinRenderer::prepareShaderMaterial(ShaderMaterial& material, RenderGraph& graph, const Outputs& outputs)
    {
        auto resourceView = [&](RenderGraph::Resource resource) -> const VriDescriptor*
        {
            // Culled stages do not read these slots, but the shared descriptor set still needs valid views.
            return resource.graph && graph.resourceInfo(resource).active ? graph.getTexture(resource).view() :
                                                                           m_Environment.radiance->view();
        };
        const std::array<const VriDescriptor*, 12> views {m_FrameView,
                                                          resourceView(outputs.shadows[0]),
                                                          resourceView(outputs.shadows[1]),
                                                          resourceView(outputs.shadows[2]),
                                                          resourceView(outputs.shadows[3]),
                                                          m_Environment.radiance->view(),
                                                          m_Environment.diffuse->view(),
                                                          m_Environment.specular->view(),
                                                          m_Environment.brdfLut->view(),
                                                          resourceView(outputs.hdr),
                                                          m_EnvironmentSampler,
                                                          m_OpenPbrLuts->view()};
        std::vector<ShaderResourceViews>           shaderResources;
        constexpr std::array                       frameNames {"frame",
                                         "shadow0",
                                         "shadow1",
                                         "shadow2",
                                         "shadow3",
                                         "radianceMap",
                                         "diffuseMap",
                                         "specularMap",
                                         "brdfLut",
                                         "hdrColor",
                                         "environmentSampler",
                                         "openPbrLuts"};
        for (uint32_t i = 0; i < frameNames.size(); ++i)
        {
            auto type = VriDescriptorType_Texture;
            if (i == 0)
            {
                type = VriDescriptorType_ConstantBuffer;
            }
            else if (i == 10)
            {
                type = VriDescriptorType_Sampler;
            }
            shaderResources.push_back({frameNames[i], type, {views[i]}});
        }
        shaderResources.push_back({"primitiveTransforms", VriDescriptorType_StructuredBuffer, {m_TransformView}});
        if (m_Scene.meshlets)
        {
            constexpr std::array names {"sceneVertices", "meshlets", "meshletVertices", "meshletTriangles"};
            for (uint32_t i = 0; i < names.size(); ++i)
            {
                shaderResources.push_back({names[i], VriDescriptorType_StructuredBuffer, {m_Scene.meshlets->views[i]}});
            }
        }
        constexpr std::array         modes {"ShadowCaster", "Forward", "GBufferBase", "GBufferMaterial", "Forward"};
        std::array<VriPipeline*, 10> pipelines {};
        for (uint32_t purpose = 0; purpose < modes.size(); ++purpose)
        {
            if ((purpose == 4 && !settings.meshShading) ||
                ((purpose == 2 || purpose == 3) && outputs.path != RenderPath::eNaiveDeferred))
            {
                continue;
            }
            ShaderPassContext context;
            context.depth      = VriFormat_D32_SFLOAT;
            context.streams    = {{sizeof(SceneVertex), 0, VriVertexStepRate_PerVertex}};
            context.attributes = {{VriFormat_RGB32_SFLOAT, uint32_t(offsetof(SceneVertex, position)), 0},
                                  {VriFormat_RGB32_SFLOAT, uint32_t(offsetof(SceneVertex, normal)), 0},
                                  {VriFormat_RG32_SFLOAT, uint32_t(offsetof(SceneVertex, uv)), 0},
                                  {VriFormat_RGBA32_SFLOAT, uint32_t(offsetof(SceneVertex, color)), 0},
                                  {VriFormat_RGBA32_SFLOAT, uint32_t(offsetof(SceneVertex, tangent)), 0}};
            if (purpose == 1 || purpose == 4)
            {
                context.colors = {VriFormat_RGBA16_SFLOAT};
            }
            else if (purpose == 2)
            {
                context.colors.assign(kGBufferFormats.begin(), kGBufferFormats.begin() + 4);
            }
            else if (purpose == 3)
            {
                context.colors.assign(kGBufferFormats.begin() + 4, kGBufferFormats.end());
                context.depthReadOnly = true;
            }
            const auto& pass = material.passForMode(modes[purpose], purpose == 4);
            for (uint32_t mirrored = 0; mirrored < 2; ++mirrored)
            {
                context.mirrored = mirrored != 0;
                auto* pipeline   = material.prepare(pass.name, context, shaderResources);
                if (material.pushConstantSize(pass.name) != 32)
                {
                    throw std::invalid_argument("Built-in shader contract requires the 32-byte vultraDraw block: " +
                                                pass.name);
                }
                pipelines[mirrored * 5 + purpose] = pipeline;
            }
        }
        return pipelines;
    }

    std::string BuiltinRenderer::shaderSubshaderCompatibility(const ShaderSubshader& subshader)
    {
        for (const auto& pass : subshader.passes)
        {
            uint32_t colors = 0;
            if (pass.lightMode == "Forward" || (pass.generated && pass.name == "MeshForward"))
            {
                colors = 1;
            }
            else if (pass.lightMode == "GBufferBase")
            {
                colors = 4;
            }
            else if (pass.lightMode == "GBufferMaterial")
            {
                colors = 3;
            }
            else if (pass.lightMode != "ShadowCaster" && pass.lightMode != "DepthOnly")
            {
                continue;
            }
            for (const auto& [variant, program] : pass.programs)
            {
                uint32_t outputs = 0;
                for (const auto& shader : program.shaders)
                {
                    outputs = std::max(outputs, shader.colorOutputs);
                }
                if (outputs != colors)
                {
                    return pass.name + " / " + variant + ": color attachment count differs from the raster contract";
                }
                const auto bindings = program.resourceBindings();
                const auto draw     = std::ranges::find(bindings, "vultraDraw", &ShaderResourceBinding::name);
                if (draw == bindings.end() || !draw->pushConstant || draw->uniformSize != 32)
                {
                    return pass.name + " / " + variant + ": requires the 32-byte vultraDraw push-constant contract";
                }
            }
        }
        return {};
    }

    std::array<uint32_t, 5> BuiltinRenderer::primitiveCounts() const
    {
        std::array<uint32_t, 5> result;
        for (size_t i = 0; i < result.size(); ++i)
        {
            result[i] = i == 0 || m_ShadowDrawn[i - 1] ? uint32_t(m_VisiblePrimitives[i].size()) : 0;
        }
        return result;
    }

    void BuiltinRenderer::completeFrame()
    {
        for (size_t i = 0; i < m_ShadowCache.size(); ++i)
        {
            if (m_RecordedShadows[i])
            {
                m_ShadowCache[i] = std::move(m_RecordedShadows[i]);
                m_RecordedShadows[i].reset();
            }
        }
    }

    void BuiltinRenderer::invalidateShadowCache()
    {
        m_ShadowCache     = {};
        m_RecordedShadows = {};
    }

    void BuiltinRenderer::prepareToneMapping(RenderGraph& graph, RenderGraph::Resource hdr)
    {
        graph.getTexture(hdr);
        m_ToneParameters = {settings.exposure, 0, double(settings.toneOperator)};
    }

    void BuiltinRenderer::setShaderMaterial(uint32_t slot, ShaderMaterial* material)
    {
        if (slot >= m_ShaderMaterials.size())
        {
            throw std::out_of_range("Shader material slot");
        }
        m_ShaderMaterials[slot] = material;
        m_ShaderPipelines[slot] = {};
    }

    void BuiltinRenderer::pollShaders()
    {
        for (uint32_t variant = 0; variant < 4; ++variant)
        {
            m_Shadow[variant]->poll();
            m_Forward[variant]->poll();
            m_GBufferBase[variant]->poll();
            m_GBufferMaterial[variant]->poll();
            if (m_MeshForward[variant])
            {
                m_MeshForward[variant]->poll();
            }
        }
        m_Skybox->poll();
        m_ToneMapping->shader().poll();
        m_DeferredLighting->poll();
    }

    std::string BuiltinRenderer::diagnostics() const
    {
        auto result =
            m_Skybox->diagnostics() + m_ToneMapping->shader().diagnostics() + m_DeferredLighting->diagnostics();
        for (uint32_t variant = 0; variant < 4; ++variant)
        {
            result += m_Shadow[variant]->diagnostics() + m_Forward[variant]->diagnostics() +
                      m_GBufferBase[variant]->diagnostics() + m_GBufferMaterial[variant]->diagnostics();
            if (m_MeshForward[variant])
            {
                result += m_MeshForward[variant]->diagnostics();
            }
        }
        return result;
    }
} // namespace vultra
